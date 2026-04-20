#include "adaptive_selector.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

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
  configure_from_env();
  ensure_initialized(active_mode);
  std::cout << "[ADAPTIVE_SELECTOR] init"
            << " window_size=" << window_size
            << " eval_stride=" << eval_stride
            << " decision_streak=" << decision_streak
            << " lock_after_switch=" << lock_after_switch
            << " initial_mode=" << mode_name(active_mode)
            << " orig_page_growth_max=" << orig_page_growth_max
            << " fdp_page_growth_max=" << fdp_page_growth_max
            << " orig_small_delta_min=" << orig_small_delta_min << std::endl;
}

void adaptive_selector::prefetcher_cycle_operate()
{
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
            << " active_mode=" << mode_name(active_mode)
            << " evaluations=" << evaluation_count
            << " switches=" << switch_count
            << " locked=" << locked
            << " demand_observations=" << demand_observations
            << " last_candidate=" << mode_name(last_candidate)
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
  window_size = std::max<std::size_t>(8, read_size_env("ADAPT_WINDOW_REFS", window_size));
  eval_stride = std::max<std::size_t>(1, read_size_env("ADAPT_EVAL_STRIDE", eval_stride));
  decision_streak = std::max<uint32_t>(1, read_u32_env("ADAPT_DECISION_STREAK", decision_streak));
  lock_after_switch = read_bool_env("ADAPT_LOCK_AFTER_SWITCH", lock_after_switch);
  orig_page_growth_max = read_double_env("ADAPT_ORIG_PAGE_GROWTH_MAX", orig_page_growth_max);
  fdp_page_growth_max = read_double_env("ADAPT_FDP_PAGE_GROWTH_MAX", fdp_page_growth_max);
  orig_small_delta_min = read_double_env("ADAPT_ORIG_SMALL_DELTA_MIN", orig_small_delta_min);

  if (const char* raw_mode = std::getenv("ADAPT_INITIAL_MODE"); raw_mode != nullptr) {
    const std::string mode{raw_mode};
    if (mode == "none") {
      active_mode = expert_mode::none;
    } else if (mode == "orig") {
      active_mode = expert_mode::orig;
    } else if (mode == "fdp") {
      active_mode = expert_mode::fdp;
    } else if (mode == "bop") {
      active_mode = expert_mode::bop;
    } else {
      throw std::runtime_error("unsupported ADAPT_INITIAL_MODE");
    }
  } else {
    active_mode = expert_mode::none;
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
  if (features.page_growth >= orig_page_growth_max) {
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
  const auto candidate = classify_window(last_features);

  if (candidate == last_candidate) {
    ++candidate_streak;
  } else {
    last_candidate = candidate;
    candidate_streak = 1;
  }

  if (candidate_streak < decision_streak || candidate == active_mode) {
    return;
  }

  const auto previous = active_mode;
  active_mode = candidate;
  ensure_initialized(active_mode);
  ++switch_count;
  std::cout << "[ADAPTIVE_SELECTOR] switch"
            << " demand_observations=" << demand_observations
            << " from=" << mode_name(previous)
            << " to=" << mode_name(active_mode)
            << " streak=" << candidate_streak
            << " rfo_share=" << last_features.rfo_share
            << " line_growth=" << last_features.line_growth
            << " page_growth=" << last_features.page_growth
            << " small_delta_ratio=" << last_features.small_delta_ratio << std::endl;

  if (lock_after_switch && active_mode != expert_mode::none) {
    locked = true;
  }
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
