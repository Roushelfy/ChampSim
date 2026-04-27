#include "adaptive_selector.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
std::size_t read_size_env(const char* name, std::size_t default_value)
{
  if (const char* raw = std::getenv(name); raw != nullptr) {
    return static_cast<std::size_t>(std::stoull(raw));
  }
  return default_value;
}

uint32_t read_u32_env(const char* name, uint32_t default_value)
{
  if (const char* raw = std::getenv(name); raw != nullptr) {
    return static_cast<uint32_t>(std::stoul(raw));
  }
  return default_value;
}

double read_double_env(const char* name, double default_value)
{
  if (const char* raw = std::getenv(name); raw != nullptr) {
    return std::stod(raw);
  }
  return default_value;
}

bool read_bool_env(const char* name, bool default_value)
{
  if (const char* raw = std::getenv(name); raw != nullptr) {
    std::string value{raw};
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
      return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
      return false;
    }
  }
  return default_value;
}

const char* read_env(const std::string& name)
{
  return std::getenv(name.c_str());
}

struct selector_shared_state {
  std::vector<double> local_pressures{};
  std::vector<uint8_t> active_modes{};
  std::vector<double> recent_rfo_share{};
  std::vector<double> recent_page_growth{};
  std::vector<double> recent_line_growth{};
  std::vector<double> recent_small_delta_ratio{};
  bool pressure_active = false;
  uint32_t high_streak = 0;
  uint32_t low_streak = 0;
};

selector_shared_state& get_selector_shared_state()
{
  static selector_shared_state state{};
  if (state.local_pressures.size() != NUM_CPUS) {
    state.local_pressures.assign(NUM_CPUS, 0.0);
    state.active_modes.assign(NUM_CPUS, 0);
    state.recent_rfo_share.assign(NUM_CPUS, 0.0);
    state.recent_page_growth.assign(NUM_CPUS, 0.0);
    state.recent_line_growth.assign(NUM_CPUS, 0.0);
    state.recent_small_delta_ratio.assign(NUM_CPUS, 0.0);
    state.pressure_active = false;
    state.high_streak = 0;
    state.low_streak = 0;
  }
  return state;
}

double max_or_zero(const std::vector<double>& values)
{
  if (values.empty()) {
    return 0.0;
  }
  return *std::max_element(values.begin(), values.end());
}

double avg_or_zero(const std::vector<double>& values)
{
  if (values.empty()) {
    return 0.0;
  }
  double total = 0.0;
  for (double value : values) {
    total += value;
  }
  return total / static_cast<double>(values.size());
}

uint32_t infer_owner_cpu_index(const CACHE* cache)
{
  const std::string& name = cache->NAME;
  if (name.rfind("cpu", 0) == 0) {
    std::size_t pos = 3;
    while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos])) != 0) {
      ++pos;
    }
    if (pos > 3) {
      return static_cast<uint32_t>(std::stoul(name.substr(3, pos - 3)));
    }
  }
  return cache->cpu;
}
} // namespace

adaptive_selector::adaptive_selector(CACHE* cache)
    : prefetcher(cache), orig_expert(cache), fdp_expert(cache), bop_expert(cache)
{
  orig_expert.prefetch_metadata_tag = static_cast<uint32_t>(expert_mode::orig);
  fdp_expert.prefetch_metadata_tag = static_cast<uint32_t>(expert_mode::fdp);
  bop_expert.prefetch_metadata_tag = static_cast<uint32_t>(expert_mode::bop);
}

void adaptive_selector::prefetcher_initialize()
{
  cpu_index = infer_owner_cpu_index(intern_);
  configure_from_env();
  auto& shared = get_selector_shared_state();
  if (cpu_index >= shared.active_modes.size()) {
    shared.active_modes.resize(cpu_index + 1, 0);
    shared.local_pressures.resize(cpu_index + 1, 0.0);
  }
  shared.active_modes[cpu_index] = static_cast<uint8_t>(active_mode);
  ensure_initialized(active_mode);
  std::cout << "[ADAPTIVE_SELECTOR] init"
            << " cpu=" << cpu_index
            << " cache_name=" << intern_->NAME
            << " window_size=" << window_size
            << " eval_stride=" << eval_stride
            << " decision_streak=" << decision_streak
            << " lock_after_switch=" << lock_after_switch
            << " initial_mode=" << mode_name(active_mode)
            << " orig_page_growth_max=" << orig_page_growth_max
            << " fdp_page_growth_max=" << fdp_page_growth_max
            << " orig_small_delta_min=" << orig_small_delta_min
            << " bop_min_observations=" << bop_min_observations
            << " disable_bop=" << disable_bop
            << " shared_coord_enable=" << shared_coord_enable
            << " shared_refresh_on_eval_only=" << shared_refresh_on_eval_only
            << " shared_pressure_use_avg=" << shared_pressure_use_avg
            << " shared_block_bop_switch=" << shared_block_bop_switch
            << " shared_demote_bop=" << shared_demote_bop
            << " shared_promote_fdp=" << shared_promote_fdp
            << " shared_peer_lbm_protect=" << shared_peer_lbm_protect
            << " shared_force_bop_demote=" << shared_force_bop_demote
            << " shared_pair_disable_bop=" << shared_pair_disable_bop
            << " shared_pair_hold_for_peer=" << shared_pair_hold_for_peer
            << " shared_pair_promote_fdp_on_peer_high_page=" << shared_pair_promote_fdp_on_peer_high_page
            << " shared_pressure_on=" << shared_pressure_on
            << " shared_pressure_off=" << shared_pressure_off
            << " shared_pressure_streak=" << shared_pressure_streak
            << " shared_bop_grant_low_streak=" << shared_bop_grant_low_streak
            << " shared_peer_lbm_rfo_min=" << shared_peer_lbm_rfo_min
            << " shared_peer_lbm_page_max=" << shared_peer_lbm_page_max
            << " shared_peer_lbm_small_delta_min=" << shared_peer_lbm_small_delta_min
            << " shared_pair_peer_lbm_enable=" << shared_pair_peer_lbm_enable
            << " shared_pair_cpu_count=" << shared_pair_cpu_count
            << " shared_pair_peer_lbm_page_max=" << shared_pair_peer_lbm_page_max
            << " shared_pair_peer_lbm_small_delta_min=" << shared_pair_peer_lbm_small_delta_min
            << " shared_peer_high_page_min=" << shared_peer_high_page_min
            << " shared_pair_promote_fdp=" << shared_pair_promote_fdp
            << " shared_pair_orig_to_fdp_page_min=" << shared_pair_orig_to_fdp_page_min
            << " shared_pair_orig_to_fdp_small_delta_max=" << shared_pair_orig_to_fdp_small_delta_max
            << " shared_pair_two_low_page_split=" << shared_pair_two_low_page_split
            << " shared_pair_low_page_max=" << shared_pair_low_page_max
            << " shared_pair_dense_delta_gap=" << shared_pair_dense_delta_gap
            << " shared_pair_bop_probation_enable=" << shared_pair_bop_probation_enable
            << " shared_pair_bop_page_min=" << shared_pair_bop_page_min
            << " shared_pair_bop_page_max=" << shared_pair_bop_page_max
            << " shared_pair_bop_peer_page_max=" << shared_pair_bop_peer_page_max
            << " shared_pair_bop_peer_small_delta_min=" << shared_pair_bop_peer_small_delta_min
            << " shared_pair_bop_pressure_max=" << shared_pair_bop_pressure_max
            << " shared_pair_high_page_symmetric_enable=" << shared_pair_high_page_symmetric_enable
            << " shared_pair_high_page_bop_min=" << shared_pair_high_page_bop_min
            << " shared_pair_high_page_bop_max=" << shared_pair_high_page_bop_max
            << " shared_pair_high_page_fdp_min=" << shared_pair_high_page_fdp_min
            << " shared_pair_high_page_sparse_delta_max=" << shared_pair_high_page_sparse_delta_max
            << " shared_pair_allow_bop_with_nondense_peer=" << shared_pair_allow_bop_with_nondense_peer
            << " shared_pair_allow_bop_page_min=" << shared_pair_allow_bop_page_min
            << " shared_pair_allow_bop_peer_small_delta_max=" << shared_pair_allow_bop_peer_small_delta_max
            << " shared_pair_allow_bop_pressure_max=" << shared_pair_allow_bop_pressure_max
            << " shared_pair_delay_low_page_fdp_lock=" << shared_pair_delay_low_page_fdp_lock
            << " shared_pair_fdp_lock_delay_evals=" << shared_pair_fdp_lock_delay_evals
            << " shared_pair_delay_fdp_page_max=" << shared_pair_delay_fdp_page_max
            << " shared_pair_delay_fdp_peer_page_max=" << shared_pair_delay_fdp_peer_page_max
            << " shared_pair_delay_fdp_small_delta_min=" << shared_pair_delay_fdp_small_delta_min
            << " shared_pair_low_page_fdp_probe_delay_enable=" << shared_pair_low_page_fdp_probe_delay_enable
            << " shared_pair_low_page_probe_page_max=" << shared_pair_low_page_probe_page_max
            << " shared_pair_low_page_probe_small_delta_max=" << shared_pair_low_page_probe_small_delta_max
            << " shared_pair_low_page_probe_streak=" << shared_pair_low_page_probe_streak
            << " shared_orig_to_fdp_page_max=" << shared_orig_to_fdp_page_max
            << " shared_orig_to_fdp_small_delta_min=" << shared_orig_to_fdp_small_delta_min
            << " shared_orig_to_fdp_small_delta_max=" << shared_orig_to_fdp_small_delta_max
            << " shared_orig_to_fdp_on_peer_high_page=" << shared_orig_to_fdp_on_peer_high_page << std::endl;
}

void adaptive_selector::prefetcher_cycle_operate()
{
  if (shared_coord_enable && !shared_refresh_on_eval_only) {
    refresh_shared_pressure();
  }
  forward_cycle_operate(active_mode);
}

uint32_t adaptive_selector::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                                     uint32_t metadata_in)
{
  const bool is_demand = (type == access_type::LOAD || type == access_type::RFO);
  if (is_demand) {
    observe_demand(addr, type);
  }

  // Preserve each expert's native view of the access stream. In particular,
  // hiding RFOs from SPP noticeably skews lbm, which is store-heavy.
  uint32_t metadata_out = forward_cache_operate(active_mode, addr, ip, cache_hit, useful_prefetch, type, strip_source_tag(metadata_in));

  if (is_demand && !locked && ready_to_evaluate()) {
    evaluate_and_update();
  }

  return metadata_out;
}

uint32_t adaptive_selector::prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  expert_mode routed_mode = active_mode;
  const uint32_t producer = source_tag(metadata_in);
  if (producer == static_cast<uint32_t>(expert_mode::orig)) {
    routed_mode = expert_mode::orig;
  } else if (producer == static_cast<uint32_t>(expert_mode::fdp)) {
    routed_mode = expert_mode::fdp;
  } else if (producer == static_cast<uint32_t>(expert_mode::bop)) {
    routed_mode = expert_mode::bop;
  }
  return forward_cache_fill(routed_mode, addr, set, way, prefetch, evicted_addr, strip_source_tag(metadata_in));
}

void adaptive_selector::prefetcher_final_stats()
{
  std::cout << "[ADAPTIVE_SELECTOR] final"
            << " cpu=" << cpu_index
            << " active_mode=" << mode_name(active_mode)
            << " evaluations=" << evaluation_count
            << " switches=" << switch_count
            << " pressure_blocks=" << pressure_block_count
            << " pressure_remaps=" << pressure_remap_count
            << " pressure_samples=" << pressure_sample_count
            << " locked=" << locked
            << " fdp_lock_delay_remaining=" << fdp_lock_delay_remaining
            << " demand_observations=" << demand_observations
            << " last_candidate=" << mode_name(last_candidate)
            << " last_local_pressure=" << last_shared.local_pressure
            << " last_global_max_pressure=" << last_shared.global_max_pressure
            << " last_global_avg_pressure=" << last_shared.global_avg_pressure
            << " last_bop_cores=" << last_shared.bop_cores
            << " last_low_pressure_streak=" << last_shared.low_pressure_streak
            << " shared_pressure_active=" << last_shared.pressure_active
            << " pair_scope_active=" << last_shared.pair_scope_active
            << " peer_lbm_like=" << last_shared.peer_lbm_like
            << " peer_lbm_pair_like=" << last_shared.peer_lbm_pair_like
            << " peer_high_page_growth=" << last_shared.peer_high_page_growth
            << " peer_pressure=" << last_shared.peer_pressure
            << " peer_page_growth=" << last_shared.peer_page_growth
            << " peer_line_growth=" << last_shared.peer_line_growth
            << " peer_small_delta_ratio=" << last_shared.peer_small_delta_ratio
            << " last_rfo_share=" << last_features.rfo_share
            << " last_line_growth=" << last_features.line_growth
            << " last_page_growth=" << last_features.page_growth
            << " last_small_delta_ratio=" << last_features.small_delta_ratio << std::endl;

  switch (active_mode) {
  case expert_mode::orig:
    orig_expert.prefetcher_final_stats();
    break;
  case expert_mode::fdp:
    fdp_expert.prefetcher_final_stats();
    break;
  case expert_mode::bop:
    bop_expert.prefetcher_final_stats();
    break;
  case expert_mode::none:
    break;
  }
}

void adaptive_selector::configure_from_env()
{
  const auto parse_mode_or_throw = [](const std::string& mode) {
    if (mode == "none") {
      return expert_mode::none;
    }
    if (mode == "orig") {
      return expert_mode::orig;
    }
    if (mode == "fdp") {
      return expert_mode::fdp;
    }
    if (mode == "bop") {
      return expert_mode::bop;
    }
    throw std::runtime_error("unsupported adaptive selector mode");
  };

  window_size = std::max<std::size_t>(8, read_size_env("ADAPT_WINDOW_REFS", window_size));
  eval_stride = std::max<std::size_t>(1, read_size_env("ADAPT_EVAL_STRIDE", eval_stride));
  decision_streak = std::max<uint32_t>(1, read_u32_env("ADAPT_DECISION_STREAK", decision_streak));
  lock_after_switch = read_bool_env("ADAPT_LOCK_AFTER_SWITCH", lock_after_switch);
  orig_page_growth_max = read_double_env("ADAPT_ORIG_PAGE_GROWTH_MAX", orig_page_growth_max);
  fdp_page_growth_max = read_double_env("ADAPT_FDP_PAGE_GROWTH_MAX", fdp_page_growth_max);
  orig_small_delta_min = read_double_env("ADAPT_ORIG_SMALL_DELTA_MIN", orig_small_delta_min);
  bop_min_observations = read_size_env("ADAPT_BOP_MIN_OBSERVATIONS", bop_min_observations);
  disable_bop = read_bool_env("ADAPT_DISABLE_BOP", disable_bop);
  shared_coord_enable = (NUM_CPUS > 1);
  shared_coord_enable = read_bool_env("ADAPT_SHARED_COORD_ENABLE", shared_coord_enable);
  shared_refresh_on_eval_only = read_bool_env("ADAPT_SHARED_REFRESH_ON_EVAL_ONLY", shared_refresh_on_eval_only);
  shared_pressure_use_avg = read_bool_env("ADAPT_SHARED_PRESSURE_USE_AVG", shared_pressure_use_avg);
  shared_block_bop_switch = read_bool_env("ADAPT_SHARED_BLOCK_BOP_SWITCH", shared_block_bop_switch);
  shared_demote_bop = read_bool_env("ADAPT_SHARED_DEMOTE_BOP", shared_demote_bop);
  shared_promote_fdp = read_bool_env("ADAPT_SHARED_PROMOTE_FDP", shared_promote_fdp);
  shared_peer_lbm_protect = read_bool_env("ADAPT_SHARED_PEER_LBM_PROTECT", shared_peer_lbm_protect);
  shared_force_bop_demote = read_bool_env("ADAPT_SHARED_FORCE_BOP_DEMOTE", shared_force_bop_demote);
  shared_pair_disable_bop = read_bool_env("ADAPT_SHARED_PAIR_DISABLE_BOP", shared_pair_disable_bop);
  shared_pair_hold_for_peer = read_bool_env("ADAPT_SHARED_PAIR_HOLD_FOR_PEER", shared_pair_hold_for_peer);
  shared_pair_promote_fdp_on_peer_high_page =
      read_bool_env("ADAPT_SHARED_PAIR_PROMOTE_FDP_ON_PEER_HIGH_PAGE", shared_pair_promote_fdp_on_peer_high_page);
  shared_pressure_on = read_double_env("ADAPT_SHARED_PRESSURE_ON", shared_pressure_on);
  shared_pressure_off = read_double_env("ADAPT_SHARED_PRESSURE_OFF", shared_pressure_off);
  shared_pressure_streak = std::max<uint32_t>(1, read_u32_env("ADAPT_SHARED_PRESSURE_STREAK", shared_pressure_streak));
  shared_bop_grant_low_streak = read_u32_env("ADAPT_SHARED_BOP_GRANT_LOW_STREAK", shared_bop_grant_low_streak);
  shared_pressure_mshr_weight = read_double_env("ADAPT_SHARED_MSHR_WEIGHT", shared_pressure_mshr_weight);
  shared_pressure_pq_weight = read_double_env("ADAPT_SHARED_PQ_WEIGHT", shared_pressure_pq_weight);
  shared_pressure_rq_weight = read_double_env("ADAPT_SHARED_RQ_WEIGHT", shared_pressure_rq_weight);
  shared_peer_lbm_rfo_min = read_double_env("ADAPT_SHARED_PEER_LBM_RFO_MIN", shared_peer_lbm_rfo_min);
  shared_peer_lbm_page_max = read_double_env("ADAPT_SHARED_PEER_LBM_PAGE_MAX", shared_peer_lbm_page_max);
  shared_peer_lbm_small_delta_min = read_double_env("ADAPT_SHARED_PEER_LBM_SMALL_DELTA_MIN", shared_peer_lbm_small_delta_min);
  shared_pair_peer_lbm_enable = read_bool_env("ADAPT_SHARED_PAIR_PEER_LBM_ENABLE", shared_pair_peer_lbm_enable);
  shared_pair_cpu_count = read_u32_env("ADAPT_SHARED_PAIR_CPU_COUNT", shared_pair_cpu_count);
  shared_pair_peer_lbm_page_max = read_double_env("ADAPT_SHARED_PAIR_PEER_LBM_PAGE_MAX", shared_pair_peer_lbm_page_max);
  shared_pair_peer_lbm_small_delta_min = read_double_env("ADAPT_SHARED_PAIR_PEER_LBM_SMALL_DELTA_MIN", shared_pair_peer_lbm_small_delta_min);
  shared_peer_high_page_min = read_double_env("ADAPT_SHARED_PEER_HIGH_PAGE_MIN", shared_peer_high_page_min);
  shared_pair_promote_fdp = read_bool_env("ADAPT_SHARED_PAIR_PROMOTE_FDP", shared_pair_promote_fdp);
  shared_pair_orig_to_fdp_page_min = read_double_env("ADAPT_SHARED_PAIR_ORIG_TO_FDP_PAGE_MIN", shared_pair_orig_to_fdp_page_min);
  shared_pair_orig_to_fdp_small_delta_max =
      read_double_env("ADAPT_SHARED_PAIR_ORIG_TO_FDP_SMALL_DELTA_MAX", shared_pair_orig_to_fdp_small_delta_max);
  shared_pair_two_low_page_split = read_bool_env("ADAPT_SHARED_PAIR_TWO_LOW_PAGE_SPLIT", shared_pair_two_low_page_split);
  shared_pair_low_page_max = read_double_env("ADAPT_SHARED_PAIR_LOW_PAGE_MAX", shared_pair_low_page_max);
  shared_pair_dense_delta_gap = read_double_env("ADAPT_SHARED_PAIR_DENSE_DELTA_GAP", shared_pair_dense_delta_gap);
  shared_pair_bop_probation_enable = read_bool_env("ADAPT_SHARED_PAIR_BOP_PROBATION_ENABLE", shared_pair_bop_probation_enable);
  shared_pair_bop_page_min = read_double_env("ADAPT_SHARED_PAIR_BOP_PAGE_MIN", shared_pair_bop_page_min);
  shared_pair_bop_page_max = read_double_env("ADAPT_SHARED_PAIR_BOP_PAGE_MAX", shared_pair_bop_page_max);
  shared_pair_bop_peer_page_max = read_double_env("ADAPT_SHARED_PAIR_BOP_PEER_PAGE_MAX", shared_pair_bop_peer_page_max);
  shared_pair_bop_peer_small_delta_min =
      read_double_env("ADAPT_SHARED_PAIR_BOP_PEER_SMALL_DELTA_MIN", shared_pair_bop_peer_small_delta_min);
  shared_pair_bop_pressure_max = read_double_env("ADAPT_SHARED_PAIR_BOP_PRESSURE_MAX", shared_pair_bop_pressure_max);
  shared_pair_high_page_symmetric_enable =
      read_bool_env("ADAPT_SHARED_PAIR_HIGH_PAGE_SYMMETRIC_ENABLE", shared_pair_high_page_symmetric_enable);
  shared_pair_high_page_bop_min = read_double_env("ADAPT_SHARED_PAIR_HIGH_PAGE_BOP_MIN", shared_pair_high_page_bop_min);
  shared_pair_high_page_bop_max = read_double_env("ADAPT_SHARED_PAIR_HIGH_PAGE_BOP_MAX", shared_pair_high_page_bop_max);
  shared_pair_high_page_fdp_min = read_double_env("ADAPT_SHARED_PAIR_HIGH_PAGE_FDP_MIN", shared_pair_high_page_fdp_min);
  shared_pair_high_page_sparse_delta_max =
      read_double_env("ADAPT_SHARED_PAIR_HIGH_PAGE_SPARSE_DELTA_MAX", shared_pair_high_page_sparse_delta_max);
  shared_pair_allow_bop_with_nondense_peer =
      read_bool_env("ADAPT_SHARED_PAIR_ALLOW_BOP_WITH_NONDENSE_PEER", shared_pair_allow_bop_with_nondense_peer);
  shared_pair_allow_bop_page_min = read_double_env("ADAPT_SHARED_PAIR_ALLOW_BOP_PAGE_MIN", shared_pair_allow_bop_page_min);
  shared_pair_allow_bop_peer_small_delta_max =
      read_double_env("ADAPT_SHARED_PAIR_ALLOW_BOP_PEER_SMALL_DELTA_MAX", shared_pair_allow_bop_peer_small_delta_max);
  shared_pair_allow_bop_pressure_max = read_double_env("ADAPT_SHARED_PAIR_ALLOW_BOP_PRESSURE_MAX", shared_pair_allow_bop_pressure_max);
  shared_pair_delay_low_page_fdp_lock = read_bool_env("ADAPT_SHARED_PAIR_DELAY_LOW_PAGE_FDP_LOCK", shared_pair_delay_low_page_fdp_lock);
  shared_pair_fdp_lock_delay_evals = read_u32_env("ADAPT_SHARED_PAIR_FDP_LOCK_DELAY_EVALS", shared_pair_fdp_lock_delay_evals);
  shared_pair_delay_fdp_page_max = read_double_env("ADAPT_SHARED_PAIR_DELAY_FDP_PAGE_MAX", shared_pair_delay_fdp_page_max);
  shared_pair_delay_fdp_peer_page_max =
      read_double_env("ADAPT_SHARED_PAIR_DELAY_FDP_PEER_PAGE_MAX", shared_pair_delay_fdp_peer_page_max);
  shared_pair_delay_fdp_small_delta_min = read_double_env("ADAPT_SHARED_PAIR_DELAY_FDP_SMALL_DELTA_MIN", shared_pair_delay_fdp_small_delta_min);
  shared_pair_low_page_fdp_probe_delay_enable =
      read_bool_env("ADAPT_SHARED_PAIR_LOW_PAGE_FDP_PROBE_DELAY_ENABLE", shared_pair_low_page_fdp_probe_delay_enable);
  shared_pair_low_page_probe_page_max =
      read_double_env("ADAPT_SHARED_PAIR_LOW_PAGE_PROBE_PAGE_MAX", shared_pair_low_page_probe_page_max);
  shared_pair_low_page_probe_small_delta_max =
      read_double_env("ADAPT_SHARED_PAIR_LOW_PAGE_PROBE_SMALL_DELTA_MAX", shared_pair_low_page_probe_small_delta_max);
  shared_pair_low_page_probe_streak =
      std::max<uint32_t>(1, read_u32_env("ADAPT_SHARED_PAIR_LOW_PAGE_PROBE_STREAK", shared_pair_low_page_probe_streak));
  shared_orig_to_fdp_page_max = read_double_env("ADAPT_SHARED_ORIG_TO_FDP_PAGE_MAX", shared_orig_to_fdp_page_max);
  shared_orig_to_fdp_small_delta_min = read_double_env("ADAPT_SHARED_ORIG_TO_FDP_SMALL_DELTA_MIN", shared_orig_to_fdp_small_delta_min);
  shared_orig_to_fdp_small_delta_max = read_double_env("ADAPT_SHARED_ORIG_TO_FDP_SMALL_DELTA_MAX", shared_orig_to_fdp_small_delta_max);
  shared_orig_to_fdp_on_peer_high_page = read_bool_env("ADAPT_SHARED_ORIG_TO_FDP_ON_PEER_HIGH_PAGE", shared_orig_to_fdp_on_peer_high_page);

  if (const char* raw_mode = std::getenv("ADAPT_INITIAL_MODE"); raw_mode != nullptr) {
    active_mode = parse_mode_or_throw(raw_mode);
  } else {
    active_mode = expert_mode::none;
  }

  const std::string cpu_prefix = "ADAPT_CPU" + std::to_string(cpu_index) + "_";
  if (const char* raw = read_env(cpu_prefix + "WINDOW_REFS"); raw != nullptr) {
    window_size = std::max<std::size_t>(8, static_cast<std::size_t>(std::stoull(raw)));
  }
  if (const char* raw = read_env(cpu_prefix + "EVAL_STRIDE"); raw != nullptr) {
    eval_stride = std::max<std::size_t>(1, static_cast<std::size_t>(std::stoull(raw)));
  }
  if (const char* raw = read_env(cpu_prefix + "DECISION_STREAK"); raw != nullptr) {
    decision_streak = std::max<uint32_t>(1, static_cast<uint32_t>(std::stoul(raw)));
  }
  if (read_env(cpu_prefix + "LOCK_AFTER_SWITCH") != nullptr) {
    lock_after_switch = read_bool_env((cpu_prefix + "LOCK_AFTER_SWITCH").c_str(), lock_after_switch);
  }
  if (const char* raw = read_env(cpu_prefix + "ORIG_PAGE_GROWTH_MAX"); raw != nullptr) {
    orig_page_growth_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "FDP_PAGE_GROWTH_MAX"); raw != nullptr) {
    fdp_page_growth_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "ORIG_SMALL_DELTA_MIN"); raw != nullptr) {
    orig_small_delta_min = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "BOP_MIN_OBSERVATIONS"); raw != nullptr) {
    bop_min_observations = static_cast<std::size_t>(std::stoull(raw));
  }
  if (read_env(cpu_prefix + "DISABLE_BOP") != nullptr) {
    disable_bop = read_bool_env((cpu_prefix + "DISABLE_BOP").c_str(), disable_bop);
  }
  if (const char* raw = read_env(cpu_prefix + "INITIAL_MODE"); raw != nullptr) {
    active_mode = parse_mode_or_throw(raw);
  }
  if (read_env(cpu_prefix + "SHARED_COORD_ENABLE") != nullptr) {
    shared_coord_enable = read_bool_env((cpu_prefix + "SHARED_COORD_ENABLE").c_str(), shared_coord_enable);
  }
  if (read_env(cpu_prefix + "SHARED_REFRESH_ON_EVAL_ONLY") != nullptr) {
    shared_refresh_on_eval_only = read_bool_env((cpu_prefix + "SHARED_REFRESH_ON_EVAL_ONLY").c_str(), shared_refresh_on_eval_only);
  }
  if (read_env(cpu_prefix + "SHARED_PRESSURE_USE_AVG") != nullptr) {
    shared_pressure_use_avg = read_bool_env((cpu_prefix + "SHARED_PRESSURE_USE_AVG").c_str(), shared_pressure_use_avg);
  }
  if (read_env(cpu_prefix + "SHARED_BLOCK_BOP_SWITCH") != nullptr) {
    shared_block_bop_switch = read_bool_env((cpu_prefix + "SHARED_BLOCK_BOP_SWITCH").c_str(), shared_block_bop_switch);
  }
  if (read_env(cpu_prefix + "SHARED_DEMOTE_BOP") != nullptr) {
    shared_demote_bop = read_bool_env((cpu_prefix + "SHARED_DEMOTE_BOP").c_str(), shared_demote_bop);
  }
  if (read_env(cpu_prefix + "SHARED_PROMOTE_FDP") != nullptr) {
    shared_promote_fdp = read_bool_env((cpu_prefix + "SHARED_PROMOTE_FDP").c_str(), shared_promote_fdp);
  }
  if (read_env(cpu_prefix + "SHARED_PEER_LBM_PROTECT") != nullptr) {
    shared_peer_lbm_protect = read_bool_env((cpu_prefix + "SHARED_PEER_LBM_PROTECT").c_str(), shared_peer_lbm_protect);
  }
  if (read_env(cpu_prefix + "SHARED_FORCE_BOP_DEMOTE") != nullptr) {
    shared_force_bop_demote = read_bool_env((cpu_prefix + "SHARED_FORCE_BOP_DEMOTE").c_str(), shared_force_bop_demote);
  }
  if (read_env(cpu_prefix + "SHARED_PAIR_DISABLE_BOP") != nullptr) {
    shared_pair_disable_bop = read_bool_env((cpu_prefix + "SHARED_PAIR_DISABLE_BOP").c_str(), shared_pair_disable_bop);
  }
  if (read_env(cpu_prefix + "SHARED_PAIR_HOLD_FOR_PEER") != nullptr) {
    shared_pair_hold_for_peer = read_bool_env((cpu_prefix + "SHARED_PAIR_HOLD_FOR_PEER").c_str(), shared_pair_hold_for_peer);
  }
  if (read_env(cpu_prefix + "SHARED_PAIR_PROMOTE_FDP_ON_PEER_HIGH_PAGE") != nullptr) {
    shared_pair_promote_fdp_on_peer_high_page =
        read_bool_env((cpu_prefix + "SHARED_PAIR_PROMOTE_FDP_ON_PEER_HIGH_PAGE").c_str(), shared_pair_promote_fdp_on_peer_high_page);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PRESSURE_ON"); raw != nullptr) {
    shared_pressure_on = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PRESSURE_OFF"); raw != nullptr) {
    shared_pressure_off = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PRESSURE_STREAK"); raw != nullptr) {
    shared_pressure_streak = std::max<uint32_t>(1, static_cast<uint32_t>(std::stoul(raw)));
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_BOP_GRANT_LOW_STREAK"); raw != nullptr) {
    shared_bop_grant_low_streak = static_cast<uint32_t>(std::stoul(raw));
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_MSHR_WEIGHT"); raw != nullptr) {
    shared_pressure_mshr_weight = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PQ_WEIGHT"); raw != nullptr) {
    shared_pressure_pq_weight = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_RQ_WEIGHT"); raw != nullptr) {
    shared_pressure_rq_weight = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PEER_LBM_RFO_MIN"); raw != nullptr) {
    shared_peer_lbm_rfo_min = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PEER_LBM_PAGE_MAX"); raw != nullptr) {
    shared_peer_lbm_page_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PEER_LBM_SMALL_DELTA_MIN"); raw != nullptr) {
    shared_peer_lbm_small_delta_min = std::stod(raw);
  }
  if (read_env(cpu_prefix + "SHARED_PAIR_PEER_LBM_ENABLE") != nullptr) {
    shared_pair_peer_lbm_enable = read_bool_env((cpu_prefix + "SHARED_PAIR_PEER_LBM_ENABLE").c_str(), shared_pair_peer_lbm_enable);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_CPU_COUNT"); raw != nullptr) {
    shared_pair_cpu_count = static_cast<uint32_t>(std::stoul(raw));
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_PEER_LBM_PAGE_MAX"); raw != nullptr) {
    shared_pair_peer_lbm_page_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_PEER_LBM_SMALL_DELTA_MIN"); raw != nullptr) {
    shared_pair_peer_lbm_small_delta_min = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PEER_HIGH_PAGE_MIN"); raw != nullptr) {
    shared_peer_high_page_min = std::stod(raw);
  }
  if (read_env(cpu_prefix + "SHARED_PAIR_PROMOTE_FDP") != nullptr) {
    shared_pair_promote_fdp = read_bool_env((cpu_prefix + "SHARED_PAIR_PROMOTE_FDP").c_str(), shared_pair_promote_fdp);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_ORIG_TO_FDP_PAGE_MIN"); raw != nullptr) {
    shared_pair_orig_to_fdp_page_min = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_ORIG_TO_FDP_SMALL_DELTA_MAX"); raw != nullptr) {
    shared_pair_orig_to_fdp_small_delta_max = std::stod(raw);
  }
  if (read_env(cpu_prefix + "SHARED_PAIR_TWO_LOW_PAGE_SPLIT") != nullptr) {
    shared_pair_two_low_page_split = read_bool_env((cpu_prefix + "SHARED_PAIR_TWO_LOW_PAGE_SPLIT").c_str(), shared_pair_two_low_page_split);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_LOW_PAGE_MAX"); raw != nullptr) {
    shared_pair_low_page_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_DENSE_DELTA_GAP"); raw != nullptr) {
    shared_pair_dense_delta_gap = std::stod(raw);
  }
  if (read_env(cpu_prefix + "SHARED_PAIR_BOP_PROBATION_ENABLE") != nullptr) {
    shared_pair_bop_probation_enable =
        read_bool_env((cpu_prefix + "SHARED_PAIR_BOP_PROBATION_ENABLE").c_str(), shared_pair_bop_probation_enable);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_BOP_PAGE_MIN"); raw != nullptr) {
    shared_pair_bop_page_min = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_BOP_PAGE_MAX"); raw != nullptr) {
    shared_pair_bop_page_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_BOP_PEER_PAGE_MAX"); raw != nullptr) {
    shared_pair_bop_peer_page_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_BOP_PEER_SMALL_DELTA_MIN"); raw != nullptr) {
    shared_pair_bop_peer_small_delta_min = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_BOP_PRESSURE_MAX"); raw != nullptr) {
    shared_pair_bop_pressure_max = std::stod(raw);
  }
  if (read_env(cpu_prefix + "SHARED_PAIR_HIGH_PAGE_SYMMETRIC_ENABLE") != nullptr) {
    shared_pair_high_page_symmetric_enable =
        read_bool_env((cpu_prefix + "SHARED_PAIR_HIGH_PAGE_SYMMETRIC_ENABLE").c_str(), shared_pair_high_page_symmetric_enable);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_HIGH_PAGE_BOP_MIN"); raw != nullptr) {
    shared_pair_high_page_bop_min = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_HIGH_PAGE_BOP_MAX"); raw != nullptr) {
    shared_pair_high_page_bop_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_HIGH_PAGE_FDP_MIN"); raw != nullptr) {
    shared_pair_high_page_fdp_min = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_HIGH_PAGE_SPARSE_DELTA_MAX"); raw != nullptr) {
    shared_pair_high_page_sparse_delta_max = std::stod(raw);
  }
  shared_pair_allow_bop_with_nondense_peer =
      read_bool_env((cpu_prefix + "SHARED_PAIR_ALLOW_BOP_WITH_NONDENSE_PEER").c_str(), shared_pair_allow_bop_with_nondense_peer);
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_ALLOW_BOP_PAGE_MIN"); raw != nullptr) {
    shared_pair_allow_bop_page_min = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_ALLOW_BOP_PEER_SMALL_DELTA_MAX"); raw != nullptr) {
    shared_pair_allow_bop_peer_small_delta_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_ALLOW_BOP_PRESSURE_MAX"); raw != nullptr) {
    shared_pair_allow_bop_pressure_max = std::stod(raw);
  }
  if (read_env(cpu_prefix + "SHARED_PAIR_DELAY_LOW_PAGE_FDP_LOCK") != nullptr) {
    shared_pair_delay_low_page_fdp_lock =
        read_bool_env((cpu_prefix + "SHARED_PAIR_DELAY_LOW_PAGE_FDP_LOCK").c_str(), shared_pair_delay_low_page_fdp_lock);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_FDP_LOCK_DELAY_EVALS"); raw != nullptr) {
    shared_pair_fdp_lock_delay_evals = static_cast<uint32_t>(std::stoul(raw));
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_DELAY_FDP_PAGE_MAX"); raw != nullptr) {
    shared_pair_delay_fdp_page_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_DELAY_FDP_PEER_PAGE_MAX"); raw != nullptr) {
    shared_pair_delay_fdp_peer_page_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_DELAY_FDP_SMALL_DELTA_MIN"); raw != nullptr) {
    shared_pair_delay_fdp_small_delta_min = std::stod(raw);
  }
  if (read_env(cpu_prefix + "SHARED_PAIR_LOW_PAGE_FDP_PROBE_DELAY_ENABLE") != nullptr) {
    shared_pair_low_page_fdp_probe_delay_enable =
        read_bool_env((cpu_prefix + "SHARED_PAIR_LOW_PAGE_FDP_PROBE_DELAY_ENABLE").c_str(), shared_pair_low_page_fdp_probe_delay_enable);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_LOW_PAGE_PROBE_PAGE_MAX"); raw != nullptr) {
    shared_pair_low_page_probe_page_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_LOW_PAGE_PROBE_SMALL_DELTA_MAX"); raw != nullptr) {
    shared_pair_low_page_probe_small_delta_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_PAIR_LOW_PAGE_PROBE_STREAK"); raw != nullptr) {
    shared_pair_low_page_probe_streak = std::max<uint32_t>(1, static_cast<uint32_t>(std::stoul(raw)));
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_ORIG_TO_FDP_PAGE_MAX"); raw != nullptr) {
    shared_orig_to_fdp_page_max = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_ORIG_TO_FDP_SMALL_DELTA_MIN"); raw != nullptr) {
    shared_orig_to_fdp_small_delta_min = std::stod(raw);
  }
  if (const char* raw = read_env(cpu_prefix + "SHARED_ORIG_TO_FDP_SMALL_DELTA_MAX"); raw != nullptr) {
    shared_orig_to_fdp_small_delta_max = std::stod(raw);
  }
  if (read_env(cpu_prefix + "SHARED_ORIG_TO_FDP_ON_PEER_HIGH_PAGE") != nullptr) {
    shared_orig_to_fdp_on_peer_high_page =
        read_bool_env((cpu_prefix + "SHARED_ORIG_TO_FDP_ON_PEER_HIGH_PAGE").c_str(), shared_orig_to_fdp_on_peer_high_page);
  }
}

void adaptive_selector::ensure_initialized(expert_mode mode)
{
  switch (mode) {
  case expert_mode::orig:
    if (!orig_initialized) {
      orig_expert.prefetcher_initialize();
      orig_initialized = true;
    }
    break;
  case expert_mode::fdp:
    if (!fdp_initialized) {
      fdp_expert.prefetcher_initialize();
      fdp_initialized = true;
    }
    break;
  case expert_mode::bop:
    if (!bop_initialized) {
      bop_expert.prefetcher_initialize();
      bop_initialized = true;
    }
    break;
  case expert_mode::none:
    break;
  }
}

void adaptive_selector::observe_demand(champsim::address addr, access_type type)
{
  ++demand_observations;
  const auto line = champsim::block_number{addr}.to<uint64_t>();
  const auto page = champsim::page_number{addr}.to<uint64_t>();
  history.push_back(window_entry{line, page, type == access_type::RFO});
  while (history.size() > window_size) {
    history.pop_front();
  }
}

bool adaptive_selector::ready_to_evaluate() const
{
  if (history.size() < window_size) {
    return false;
  }
  return ((demand_observations - window_size) % eval_stride) == 0;
}

adaptive_selector::window_features adaptive_selector::compute_window_features() const
{
  window_features features{};
  features.observations = history.size();
  if (history.empty()) {
    return features;
  }

  std::unordered_set<uint64_t> unique_lines;
  std::unordered_set<uint64_t> unique_pages;
  unique_lines.reserve(history.size());
  unique_pages.reserve(history.size());

  std::size_t rfo_count = 0;
  std::size_t small_delta_count = 0;
  bool have_prev_line = false;
  uint64_t prev_line = 0;

  for (const auto& entry : history) {
    unique_lines.insert(entry.line);
    unique_pages.insert(entry.page);
    rfo_count += static_cast<std::size_t>(entry.is_rfo);

    if (have_prev_line) {
      const auto delta = (entry.line >= prev_line) ? (entry.line - prev_line) : (prev_line - entry.line);
      if (delta <= 4) {
        ++small_delta_count;
      }
    }

    prev_line = entry.line;
    have_prev_line = true;
  }

  features.rfo_share = static_cast<double>(rfo_count) / static_cast<double>(history.size());
  features.line_growth = static_cast<double>(unique_lines.size()) / static_cast<double>(history.size());
  features.page_growth = static_cast<double>(unique_pages.size()) / static_cast<double>(history.size());
  features.small_delta_ratio = history.size() > 1 ? static_cast<double>(small_delta_count) / static_cast<double>(history.size() - 1) : 0.0;
  return features;
}

adaptive_selector::expert_mode adaptive_selector::classify_window(const window_features& features) const
{
  if (!disable_bop && demand_observations >= bop_min_observations && features.page_growth >= orig_page_growth_max) {
    return expert_mode::bop;
  }
  if (features.small_delta_ratio >= orig_small_delta_min) {
    return expert_mode::orig;
  }
  if (features.page_growth < fdp_page_growth_max) {
    return expert_mode::fdp;
  }
  return expert_mode::orig;
}

void adaptive_selector::evaluate_and_update()
{
  ++evaluation_count;
  last_features = compute_window_features();
  auto& shared = get_selector_shared_state();
  if (cpu_index >= shared.recent_rfo_share.size()) {
    shared.recent_rfo_share.resize(cpu_index + 1, 0.0);
    shared.recent_page_growth.resize(cpu_index + 1, 0.0);
    shared.recent_line_growth.resize(cpu_index + 1, 0.0);
    shared.recent_small_delta_ratio.resize(cpu_index + 1, 0.0);
  }
  shared.recent_rfo_share[cpu_index] = last_features.rfo_share;
  shared.recent_page_growth[cpu_index] = last_features.page_growth;
  shared.recent_line_growth[cpu_index] = last_features.line_growth;
  shared.recent_small_delta_ratio[cpu_index] = last_features.small_delta_ratio;
  if (shared_coord_enable) {
    refresh_shared_pressure();
  }

  const bool waiting_for_peer = shared_coord_enable && active_mode == expert_mode::none && last_shared.pair_scope_active && shared_pair_hold_for_peer &&
                                last_shared.peer_ready_cores == 0;
  if (waiting_for_peer) {
    return;
  }

  const auto base_candidate = classify_window(last_features);
  const auto candidate = coordinate_candidate(base_candidate, last_features);

  if (candidate == last_candidate) {
    ++candidate_streak;
  } else {
    last_candidate = candidate;
    candidate_streak = 1;
  }

  if (shared_coord_enable && shared_block_bop_switch && candidate == expert_mode::bop && active_mode != expert_mode::bop &&
      (last_shared.pressure_active || last_shared.peer_lbm_like ||
       (shared_bop_grant_low_streak > 0 && last_shared.low_pressure_streak < shared_bop_grant_low_streak))) {
    ++pressure_block_count;
    return;
  }

  uint32_t required_streak = decision_streak;
  const bool low_page_orig_probe =
      shared_pair_low_page_fdp_probe_delay_enable && active_mode == expert_mode::none && last_shared.pair_scope_active &&
      last_shared.peer_ready_cores > 0 && base_candidate == expert_mode::fdp && candidate == expert_mode::orig &&
      last_features.page_growth < shared_pair_low_page_probe_page_max &&
      last_features.small_delta_ratio < shared_pair_low_page_probe_small_delta_max && last_shared.peer_page_growth > 0.0 &&
      last_shared.peer_page_growth < shared_pair_low_page_max &&
      last_shared.peer_small_delta_ratio >= last_features.small_delta_ratio + shared_pair_dense_delta_gap;
  const bool low_page_dense_peer_probe =
      shared_pair_low_page_fdp_probe_delay_enable && active_mode == expert_mode::none && last_shared.pair_scope_active &&
      last_shared.peer_ready_cores > 0 && candidate == expert_mode::orig && last_features.page_growth < fdp_page_growth_max &&
      last_features.small_delta_ratio >= orig_small_delta_min && last_shared.peer_page_growth >= fdp_page_growth_max &&
      last_shared.peer_page_growth < shared_pair_low_page_probe_page_max &&
      last_shared.peer_small_delta_ratio < shared_pair_low_page_probe_small_delta_max;
  if (low_page_orig_probe || low_page_dense_peer_probe) {
    required_streak = std::max(required_streak, shared_pair_low_page_probe_streak);
  }
  if (candidate_streak < required_streak || candidate == active_mode) {
    if (active_mode == expert_mode::fdp && fdp_lock_delay_remaining > 0) {
      --fdp_lock_delay_remaining;
      if (fdp_lock_delay_remaining == 0 && lock_after_switch) {
        locked = true;
      }
    }
    return;
  }

  const auto previous = active_mode;
  active_mode = candidate;
  if (cpu_index < shared.active_modes.size()) {
    shared.active_modes[cpu_index] = static_cast<uint8_t>(active_mode);
  }
  ensure_initialized(active_mode);
  ++switch_count;
  std::cout << "[ADAPTIVE_SELECTOR] switch"
            << " cpu=" << cpu_index
            << " demand_observations=" << demand_observations
            << " from=" << mode_name(previous)
            << " to=" << mode_name(active_mode)
            << " streak=" << candidate_streak
            << " required_streak=" << required_streak
            << " pressure_active=" << last_shared.pressure_active
            << " local_pressure=" << last_shared.local_pressure
            << " global_pressure=" << (shared_pressure_use_avg ? last_shared.global_avg_pressure : last_shared.global_max_pressure)
            << " peer_pressure=" << last_shared.peer_pressure
            << " peer_page_growth=" << last_shared.peer_page_growth
            << " peer_small_delta_ratio=" << last_shared.peer_small_delta_ratio
            << " rfo_share=" << last_features.rfo_share
            << " line_growth=" << last_features.line_growth
            << " page_growth=" << last_features.page_growth
            << " small_delta_ratio=" << last_features.small_delta_ratio << std::endl;

  fdp_lock_delay_remaining = 0;
  if (lock_after_switch && active_mode != expert_mode::none) {
    const bool delay_fdp_lock = previous == expert_mode::none && active_mode == expert_mode::fdp && should_delay_low_page_fdp_lock(last_features);
    if (delay_fdp_lock) {
      fdp_lock_delay_remaining = shared_pair_fdp_lock_delay_evals;
      locked = (fdp_lock_delay_remaining == 0);
    } else {
      locked = true;
    }
  }
}

double adaptive_selector::compute_local_pressure() const
{
  const double mshr = intern_->get_mshr_occupancy_ratio();
  const double pq = max_or_zero(intern_->get_pq_occupancy_ratio());
  const double rq = max_or_zero(intern_->get_rq_occupancy_ratio());

  const double weighted = (shared_pressure_mshr_weight * mshr) + (shared_pressure_pq_weight * pq) + (shared_pressure_rq_weight * rq);
  return std::clamp(weighted, 0.0, 1.0);
}

void adaptive_selector::refresh_shared_pressure()
{
  if (!shared_coord_enable) {
    return;
  }

  ++pressure_sample_count;
  auto& shared = get_selector_shared_state();
  if (cpu_index >= shared.local_pressures.size()) {
    shared.local_pressures.resize(cpu_index + 1, 0.0);
    shared.active_modes.resize(cpu_index + 1, 0);
    shared.recent_rfo_share.resize(cpu_index + 1, 0.0);
    shared.recent_page_growth.resize(cpu_index + 1, 0.0);
    shared.recent_line_growth.resize(cpu_index + 1, 0.0);
    shared.recent_small_delta_ratio.resize(cpu_index + 1, 0.0);
  }

  last_shared.local_pressure = compute_local_pressure();
  shared.local_pressures[cpu_index] = last_shared.local_pressure;
  shared.active_modes[cpu_index] = static_cast<uint8_t>(active_mode);

  last_shared.global_max_pressure = max_or_zero(shared.local_pressures);
  last_shared.global_avg_pressure = avg_or_zero(shared.local_pressures);
  last_shared.peer_pressure = 0.0;
  last_shared.peer_page_growth = 0.0;
  last_shared.peer_line_growth = 0.0;
  last_shared.peer_small_delta_ratio = 0.0;
  last_shared.bop_cores = static_cast<uint32_t>(std::count(shared.active_modes.begin(), shared.active_modes.end(), static_cast<uint8_t>(expert_mode::bop)));
  last_shared.low_pressure_streak = shared.low_streak;

  const bool pair_scope_requested = shared_pair_peer_lbm_enable || shared_pair_promote_fdp;
  const bool pair_signature_requested = shared_pair_disable_bop || shared_pair_hold_for_peer || shared_pair_promote_fdp_on_peer_high_page;
  last_shared.pair_scope_active =
      (pair_scope_requested || pair_signature_requested) &&
      (shared_pair_cpu_count == 0 || shared.active_modes.size() == static_cast<std::size_t>(shared_pair_cpu_count));
  last_shared.peer_lbm_like = false;
  last_shared.peer_lbm_pair_like = false;
  last_shared.peer_high_page_growth = false;
  last_shared.peer_ready_cores = 0;
  for (std::size_t idx = 0; idx < shared.recent_rfo_share.size(); ++idx) {
    if (idx == cpu_index) {
      continue;
    }
    const double peer_page_growth = shared.recent_page_growth[idx];
    if (peer_page_growth <= 0.0) {
      continue;
    }
    ++last_shared.peer_ready_cores;
    last_shared.peer_pressure = std::max(last_shared.peer_pressure, shared.local_pressures[idx]);
    last_shared.peer_page_growth = std::max(last_shared.peer_page_growth, peer_page_growth);
    last_shared.peer_line_growth = std::max(last_shared.peer_line_growth, shared.recent_line_growth[idx]);
    last_shared.peer_small_delta_ratio = std::max(last_shared.peer_small_delta_ratio, shared.recent_small_delta_ratio[idx]);
    const bool page_in_lbm_band = peer_page_growth <= shared_peer_lbm_page_max;
    const bool rfo_lbm_like = page_in_lbm_band && shared.recent_rfo_share[idx] >= shared_peer_lbm_rfo_min;
    const bool delta_lbm_like = page_in_lbm_band && shared.recent_small_delta_ratio[idx] >= shared_peer_lbm_small_delta_min;
    const bool pair_lbm_like = last_shared.pair_scope_active && peer_page_growth <= shared_pair_peer_lbm_page_max &&
                               shared.recent_small_delta_ratio[idx] >= shared_pair_peer_lbm_small_delta_min;
    if (rfo_lbm_like || delta_lbm_like || (shared_pair_peer_lbm_enable && pair_lbm_like)) {
      last_shared.peer_lbm_like = true;
    }
    if (pair_lbm_like) {
      last_shared.peer_lbm_pair_like = true;
    }
    if (peer_page_growth >= shared_peer_high_page_min) {
      last_shared.peer_high_page_growth = true;
    }
  }

  const double pressure_signal = shared_pressure_use_avg ? last_shared.global_avg_pressure : last_shared.global_max_pressure;
  if (pressure_signal >= shared_pressure_on) {
    ++shared.high_streak;
    shared.low_streak = 0;
  } else if (pressure_signal <= shared_pressure_off) {
    ++shared.low_streak;
    shared.high_streak = 0;
  }

  if (!shared.pressure_active && shared.high_streak >= shared_pressure_streak) {
    shared.pressure_active = true;
  } else if (shared.pressure_active && shared.low_streak >= shared_pressure_streak) {
    shared.pressure_active = false;
  }
  last_shared.pressure_active = shared.pressure_active;

  if (shared_coord_enable && shared_force_bop_demote && active_mode == expert_mode::bop && (last_shared.pressure_active || last_shared.peer_lbm_like)) {
    active_mode = (last_shared.peer_lbm_like ? expert_mode::orig : expert_mode::fdp);
    shared.active_modes[cpu_index] = static_cast<uint8_t>(active_mode);
    ++switch_count;
    ++pressure_remap_count;
    last_candidate = active_mode;
    fdp_lock_delay_remaining = 0;
    candidate_streak = 0;
    ensure_initialized(active_mode);
    std::cout << "[ADAPTIVE_SELECTOR] force_demote"
              << " cpu=" << cpu_index
              << " to=" << mode_name(active_mode)
              << " pressure_active=" << last_shared.pressure_active
              << " peer_lbm_like=" << last_shared.peer_lbm_like
              << " local_pressure=" << last_shared.local_pressure
              << " global_pressure=" << (shared_pressure_use_avg ? last_shared.global_avg_pressure : last_shared.global_max_pressure)
              << std::endl;
  }
}

adaptive_selector::expert_mode adaptive_selector::coordinate_candidate(expert_mode candidate, const window_features& features)
{
  if (!shared_coord_enable) {
    return candidate;
  }

  const bool pair_ready = last_shared.pair_scope_active && last_shared.peer_ready_cores > 0;
  const bool sparse_peer = last_shared.peer_small_delta_ratio <= shared_pair_high_page_sparse_delta_max;
  if (pair_ready && shared_pair_high_page_symmetric_enable && candidate == expert_mode::bop &&
      features.small_delta_ratio <= shared_pair_high_page_sparse_delta_max) {
    const bool both_ultra_high_page =
        features.page_growth >= shared_pair_high_page_fdp_min && last_shared.peer_page_growth >= shared_pair_high_page_fdp_min && sparse_peer;
    if (both_ultra_high_page) {
      ++pressure_remap_count;
      return expert_mode::fdp;
    }

    const bool both_mid_high_page =
        features.page_growth >= shared_pair_high_page_bop_min && features.page_growth <= shared_pair_high_page_bop_max &&
        last_shared.peer_page_growth >= shared_pair_high_page_bop_min && last_shared.peer_page_growth <= shared_pair_high_page_bop_max &&
        sparse_peer;
    if (both_mid_high_page) {
      return candidate;
    }
  }

  const bool peer_is_dense_low_page =
      pair_ready && last_shared.peer_page_growth <= shared_pair_peer_lbm_page_max &&
      last_shared.peer_small_delta_ratio >= shared_pair_peer_lbm_small_delta_min;
  const bool allow_bop_with_nondense_peer =
      pair_ready && shared_pair_allow_bop_with_nondense_peer && candidate == expert_mode::bop && !last_shared.pressure_active &&
      last_shared.local_pressure <= shared_pair_allow_bop_pressure_max && features.page_growth >= shared_pair_allow_bop_page_min &&
      features.small_delta_ratio <= shared_pair_allow_bop_peer_small_delta_max &&
      last_shared.peer_small_delta_ratio <= shared_pair_allow_bop_peer_small_delta_max && !peer_is_dense_low_page;
  if (allow_bop_with_nondense_peer) {
    return candidate;
  }

  const bool both_low_page =
      pair_ready && shared_pair_two_low_page_split && features.page_growth < shared_pair_low_page_max &&
      last_shared.peer_page_growth > 0.0 && last_shared.peer_page_growth < shared_pair_low_page_max;
  if (both_low_page) {
    if (features.small_delta_ratio >= last_shared.peer_small_delta_ratio + shared_pair_dense_delta_gap) {
      if (candidate != expert_mode::fdp) {
        ++pressure_remap_count;
      }
      return expert_mode::fdp;
    }
    if (last_shared.peer_small_delta_ratio >= features.small_delta_ratio + shared_pair_dense_delta_gap) {
      // For sparse core paired with dense peer, prefer FDP to reduce interference
      if (candidate != expert_mode::fdp) {
        ++pressure_remap_count;
      }
      return expert_mode::fdp;
    }
  }

  const bool allow_pair_bop_probation =
      pair_ready && shared_pair_bop_probation_enable && candidate == expert_mode::bop && !last_shared.pressure_active &&
      last_shared.local_pressure <= shared_pair_bop_pressure_max && features.page_growth >= shared_pair_bop_page_min &&
      features.page_growth <= shared_pair_bop_page_max && last_shared.peer_page_growth <= shared_pair_bop_peer_page_max &&
      last_shared.peer_small_delta_ratio >= shared_pair_bop_peer_small_delta_min;

  if (allow_pair_bop_probation) {
    return candidate;
  }

  if (last_shared.pair_scope_active && shared_pair_disable_bop && candidate == expert_mode::bop) {
    ++pressure_remap_count;
    return expert_mode::fdp;
  }

  if (last_shared.pair_scope_active && shared_pair_promote_fdp_on_peer_high_page && candidate == expert_mode::orig &&
      last_shared.peer_high_page_growth && features.page_growth < fdp_page_growth_max) {
    ++pressure_remap_count;
    return expert_mode::fdp;
  }

  if (candidate == expert_mode::bop && last_shared.peer_lbm_like && shared_peer_lbm_protect) {
    ++pressure_remap_count;
    return expert_mode::fdp;
  }

  // When paired with lbm-like peer, sparse workloads should use FDP not orig
  if (pair_ready && last_shared.peer_lbm_like && candidate == expert_mode::orig &&
      features.page_growth >= orig_page_growth_max &&
      features.small_delta_ratio < orig_small_delta_min) {
    ++pressure_remap_count;
    return expert_mode::fdp;
  }

  // In pair mode, if this core looks lbm-like (dense streaming, low page growth, high RFO)
  // AND the peer looks like a sparse/high-page-growth workload,
  // prefer FDP over orig to reduce bandwidth pressure on shared resources.
  if (pair_ready && candidate == expert_mode::orig &&
      features.page_growth < fdp_page_growth_max &&
      features.small_delta_ratio >= orig_small_delta_min &&
      features.rfo_share >= shared_peer_lbm_rfo_min &&
      last_shared.peer_high_page_growth) {
    ++pressure_remap_count;
    return expert_mode::fdp;
  }

  if (!last_shared.pressure_active) {
    return candidate;
  }

  if (candidate == expert_mode::bop && shared_demote_bop) {
    ++pressure_remap_count;
    if (features.page_growth < fdp_page_growth_max) {
      return expert_mode::fdp;
    }
    return expert_mode::orig;
  }

  if (candidate == expert_mode::orig && shared_pair_promote_fdp && last_shared.pair_scope_active && last_shared.peer_lbm_pair_like &&
      features.page_growth >= shared_pair_orig_to_fdp_page_min && features.small_delta_ratio < shared_pair_orig_to_fdp_small_delta_max) {
    ++pressure_remap_count;
    return expert_mode::fdp;
  }

  if (candidate == expert_mode::orig && shared_promote_fdp && features.page_growth < shared_orig_to_fdp_page_max &&
      features.small_delta_ratio >= shared_orig_to_fdp_small_delta_min && features.small_delta_ratio < shared_orig_to_fdp_small_delta_max &&
      (last_shared.pressure_active || (shared_orig_to_fdp_on_peer_high_page && last_shared.peer_high_page_growth))) {
    ++pressure_remap_count;
    return expert_mode::fdp;
  }

  return candidate;
}

bool adaptive_selector::should_delay_low_page_fdp_lock(const window_features& features) const
{
  if (!shared_pair_delay_low_page_fdp_lock || shared_pair_fdp_lock_delay_evals == 0 || !last_shared.pair_scope_active ||
      last_shared.peer_ready_cores == 0) {
    return false;
  }
  if (features.page_growth > shared_pair_delay_fdp_page_max || last_shared.peer_page_growth <= 0.0 ||
      last_shared.peer_page_growth > shared_pair_delay_fdp_peer_page_max) {
    return false;
  }

  const bool local_near_orig = features.small_delta_ratio >= shared_pair_delay_fdp_small_delta_min;
  const bool peer_near_orig = last_shared.peer_small_delta_ratio >= shared_pair_delay_fdp_small_delta_min;
  return local_near_orig && peer_near_orig;
}

uint32_t adaptive_selector::forward_cache_operate(expert_mode mode, champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch,
                                                  access_type type, uint32_t metadata_in)
{
  switch (mode) {
  case expert_mode::orig:
    ensure_initialized(mode);
    return orig_expert.prefetcher_cache_operate(addr, ip, cache_hit, useful_prefetch, type, metadata_in);
  case expert_mode::fdp:
    ensure_initialized(mode);
    return fdp_expert.prefetcher_cache_operate(addr, ip, cache_hit, useful_prefetch, type, metadata_in);
  case expert_mode::bop:
    ensure_initialized(mode);
    return bop_expert.prefetcher_cache_operate(addr, ip, cache_hit, useful_prefetch, type, metadata_in);
  case expert_mode::none:
    return metadata_in;
  }
  return metadata_in;
}

uint32_t adaptive_selector::forward_cache_fill(expert_mode mode, champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr,
                                               uint32_t metadata_in)
{
  switch (mode) {
  case expert_mode::orig:
    ensure_initialized(mode);
    return orig_expert.prefetcher_cache_fill(addr, set, way, prefetch, evicted_addr, metadata_in);
  case expert_mode::fdp:
    ensure_initialized(mode);
    return fdp_expert.prefetcher_cache_fill(addr, set, way, prefetch, evicted_addr, metadata_in);
  case expert_mode::bop:
    ensure_initialized(mode);
    return bop_expert.prefetcher_cache_fill(addr, set, way, prefetch, evicted_addr, metadata_in);
  case expert_mode::none:
    return metadata_in;
  }
  return metadata_in;
}

void adaptive_selector::forward_cycle_operate(expert_mode mode)
{
  switch (mode) {
  case expert_mode::orig:
    ensure_initialized(mode);
    orig_expert.prefetcher_cycle_operate();
    break;
  case expert_mode::fdp:
    ensure_initialized(mode);
    fdp_expert.prefetcher_cycle_operate();
    break;
  case expert_mode::bop:
    ensure_initialized(mode);
    bop_expert.prefetcher_cycle_operate();
    break;
  case expert_mode::none:
    break;
  }
}

const char* adaptive_selector::mode_name(expert_mode mode)
{
  switch (mode) {
  case expert_mode::none:
    return "none";
  case expert_mode::orig:
    return "orig";
  case expert_mode::fdp:
    return "fdp";
  case expert_mode::bop:
    return "bop";
  }
  return "unknown";
}

uint32_t adaptive_selector::source_tag(uint32_t metadata)
{
  return (metadata >> 24) & 0xFFu;
}

uint32_t adaptive_selector::strip_source_tag(uint32_t metadata)
{
  return metadata & 0x00FFFFFFu;
}
