#include "spp_gsp_tiered.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>

namespace
{
constexpr std::size_t GSP_MAX_TRACKED_CPUS = 16;

std::array<bool, GSP_MAX_TRACKED_CPUS> shared_cpu_active{};
std::array<double, GSP_MAX_TRACKED_CPUS> shared_last_accuracy{};
std::array<double, GSP_MAX_TRACKED_CPUS> shared_last_mshr_util{};
std::array<double, GSP_MAX_TRACKED_CPUS> shared_last_llc_rq_util{};

struct shared_gsp_snapshot {
  std::size_t active_cores = 0;
  bool any_low_accuracy = false;
  uint32_t low_accuracy_cores = 0;
  double avg_accuracy = 0.0;
  double avg_mshr_util = 0.0;
  double max_mshr_util = 0.0;
  double avg_llc_rq_util = 0.0;
  double max_llc_rq_util = 0.0;
};

void maybe_set_double(const char* key, double& out)
{
  if (const char* raw = std::getenv(key)) {
    try {
      out = std::stod(raw);
    } catch (...) {
    }
  }
}

void maybe_set_uint(const char* key, uint32_t& out)
{
  if (const char* raw = std::getenv(key)) {
    try {
      out = static_cast<uint32_t>(std::stoul(raw));
    } catch (...) {
    }
  }
}

void maybe_set_string(const char* key, std::string& out)
{
  if (const char* raw = std::getenv(key))
    out = raw;
}

void maybe_set_pressure_mode(const char* key, spp_gsp_tiered::pressure_mode_t& mode, std::string& label)
{
  if (const char* raw = std::getenv(key)) {
    const std::string value = raw;
    if (value == "avg") {
      mode = spp_gsp_tiered::pressure_mode_t::avg;
      label = "avg";
    } else if (value == "max") {
      mode = spp_gsp_tiered::pressure_mode_t::max;
      label = "max";
    } else if (value == "avg+max" || value == "avg_or_max") {
      mode = spp_gsp_tiered::pressure_mode_t::avg_or_max;
      label = "avg+max";
    }
  }
}

void maybe_set_low_accuracy_mode(const char* key, spp_gsp_tiered::low_accuracy_mode_t& mode, std::string& label)
{
  if (const char* raw = std::getenv(key)) {
    const std::string value = raw;
    if (value == "any") {
      mode = spp_gsp_tiered::low_accuracy_mode_t::any;
      label = "any";
    } else if (value == "all") {
      mode = spp_gsp_tiered::low_accuracy_mode_t::all;
      label = "all";
    } else if (value == "pressure" || value == "pressure_override") {
      mode = spp_gsp_tiered::low_accuracy_mode_t::pressure_override;
      label = "pressure";
    }
  }
}

void publish_shared_epoch_state(std::size_t cpu, double accuracy, double mshr_util, double llc_rq_util)
{
  if (cpu >= GSP_MAX_TRACKED_CPUS)
    return;

  shared_cpu_active[cpu] = true;
  shared_last_accuracy[cpu] = accuracy;
  shared_last_mshr_util[cpu] = mshr_util;
  shared_last_llc_rq_util[cpu] = llc_rq_util;
}

shared_gsp_snapshot snapshot_shared_epoch_state(double low_accuracy_threshold)
{
  shared_gsp_snapshot snapshot;

  for (std::size_t cpu = 0; cpu < GSP_MAX_TRACKED_CPUS; ++cpu) {
    if (!shared_cpu_active[cpu])
      continue;

    ++snapshot.active_cores;
    snapshot.avg_accuracy += shared_last_accuracy[cpu];
    snapshot.avg_mshr_util += shared_last_mshr_util[cpu];
    snapshot.avg_llc_rq_util += shared_last_llc_rq_util[cpu];
    snapshot.max_mshr_util = std::max(snapshot.max_mshr_util, shared_last_mshr_util[cpu]);
    snapshot.max_llc_rq_util = std::max(snapshot.max_llc_rq_util, shared_last_llc_rq_util[cpu]);
    if (shared_last_accuracy[cpu] < low_accuracy_threshold) {
      snapshot.any_low_accuracy = true;
      ++snapshot.low_accuracy_cores;
    }
  }

  if (snapshot.active_cores > 0) {
    snapshot.avg_accuracy /= static_cast<double>(snapshot.active_cores);
    snapshot.avg_mshr_util /= static_cast<double>(snapshot.active_cores);
    snapshot.avg_llc_rq_util /= static_cast<double>(snapshot.active_cores);
  }

  return snapshot;
}
} // namespace

void spp_gsp_tiered::prefetcher_initialize()
{
  load_runtime_params();
  std::cout << "Initialize SIGNATURE TABLE" << std::endl;
  std::cout << "ST_SET: " << ST_SET << std::endl;
  std::cout << "ST_WAY: " << ST_WAY << std::endl;
  std::cout << "ST_TAG_BIT: " << ST_TAG_BIT << std::endl;

  std::cout << std::endl << "Initialize PATTERN TABLE" << std::endl;
  std::cout << "PT_SET: " << PT_SET << std::endl;
  std::cout << "PT_WAY: " << PT_WAY << std::endl;
  std::cout << "SIG_DELTA_BIT: " << SIG_DELTA_BIT << std::endl;
  std::cout << "C_SIG_BIT: " << C_SIG_BIT << std::endl;
  std::cout << "C_DELTA_BIT: " << C_DELTA_BIT << std::endl;

  std::cout << std::endl << "Initialize PREFETCH FILTER" << std::endl;
  std::cout << "FILTER_SET: " << FILTER_SET << std::endl;

  // pass pointers
  ST._parent = this;
  PT._parent = this;
  FILTER._parent = this;
  GHR._parent = this;

  std::cout << "[" << VARIANT_TAG << "] cpu=" << intern_->cpu
            << " candidate_id=" << params.candidate_id
            << " pressure_mode=" << params.pressure_mode_label
            << " low_acc_mode=" << params.low_accuracy_mode_label
            << " low_acc_override_tier=" << params.low_accuracy_override_tier
            << " acc_low_throttle=" << params.acc_low_throttle
            << " local_runway_mshr=" << params.local_runway_mshr
            << " local_runway_llc_rq=" << params.local_runway_llc_rq
            << " burst_activation_gap=" << params.burst_activation_gap
            << " local_throttle_patience=" << params.local_throttle_patience
            << " local_relief_patience=" << params.local_relief_patience
            << " global_mshr_t1=" << params.global_mshr_t1
            << " global_mshr_t2=" << params.global_mshr_t2
            << " global_mshr_t3=" << params.global_mshr_t3
            << " global_llc_t1=" << params.global_llc_t1
            << " global_llc_t2=" << params.global_llc_t2
            << " global_llc_t3=" << params.global_llc_t3
            << " tier1_issue_period=" << params.tier1_issue_period
            << " tier2_issue_period=" << params.tier2_issue_period
            << " tier3_pf_threshold=" << params.tier3_pf_threshold
            << " min_active_cores=" << params.min_active_cores
            << " congested_epochs=" << params.congested_epochs
            << " relaxed_epochs=" << params.relaxed_epochs
            << std::endl;
}

void spp_gsp_tiered::prefetcher_cycle_operate() {}

uint32_t spp_gsp_tiered::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                           uint32_t metadata_in)
{
  // ── BWC: epoch trigger ────────────────────────────────────────────────────
  record_pressure_sample();
  ++fdp_access_count;
  if (fdp_access_count >= FDP_EPOCH_SIZE) {
    finalize_pressure_epoch();
    bwc_update_epoch();
    fdp_access_count    = 0;
    fdp_epoch_pf_issued = 0;
    fdp_epoch_pf_useful = 0;
  }
  // ─────────────────────────────────────────────────────────────────────────

  champsim::page_number page{addr};
  uint32_t last_sig = 0, curr_sig = 0, depth = 0;
  std::vector<uint32_t> confidence_q(intern_->MSHR_SIZE);

  typename spp_gsp_tiered::offset_type::difference_type delta = 0;
  std::vector<typename spp_gsp_tiered::offset_type::difference_type> delta_q(intern_->MSHR_SIZE);

  for (uint32_t i = 0; i < intern_->MSHR_SIZE; i++) {
    confidence_q[i] = 0;
    delta_q[i] = 0;
  }
  confidence_q[0] = 100;
  GHR.global_accuracy = GHR.pf_issued ? ((100 * GHR.pf_useful) / GHR.pf_issued) : 0;

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << std::endl << "[ChampSim] " << __func__ << " addr: " << addr;
    std::cout << " page: " << page << std::endl;
  }

  // Stage 1: Read and update a sig stored in ST
  // last_sig and delta are used to update (sig, delta) correlation in PT
  // curr_sig is used to read prefetch candidates in PT
  ST.read_and_update_sig(addr, last_sig, curr_sig, delta);

  // Also check the prefetch filter in parallel to update global accuracy counters
  FILTER.check(addr, spp_gsp_tiered::L2C_DEMAND);

  // Stage 2: Update delta patterns stored in PT
  if (last_sig)
    PT.update_pattern(last_sig, delta);

  // Stage 3: Start prefetching
  auto base_addr = addr;
  uint32_t lookahead_conf = 100, pf_q_head = 0, pf_q_tail = 0;
  uint8_t do_lookahead = 0;

  do {
    uint32_t lookahead_way = PT_WAY;
    PT.read_pattern(curr_sig, delta_q, confidence_q, lookahead_way, lookahead_conf, pf_q_tail, depth, pf_threshold);

    do_lookahead = 0;
    for (uint32_t i = pf_q_head; i < pf_q_tail; i++) {
      if (confidence_q[i] >= pf_threshold) {
        champsim::address pf_addr{champsim::block_number{base_addr} + delta_q[i]};

        if (champsim::page_number{pf_addr} == page) { // Prefetch request is in the same physical page
          // BWC rate limiter: gate BEFORE FILTER.check() (which is a combined check-and-set).
          // Skipping after FILTER.check() would mark the address as prefetched without issuing.
          if (bwc_should_issue()) {
            if (FILTER.check(pf_addr, ((confidence_q[i] >= fill_threshold) ? spp_gsp_tiered::SPP_L2C_PREFETCH : spp_gsp_tiered::SPP_LLC_PREFETCH))) {
              prefetch_line(pf_addr, (confidence_q[i] >= fill_threshold), 0); // Use addr (not base_addr) to obey the same physical page boundary

              ++fdp_epoch_pf_issued; // BWC: count ALL issued prefetches (LLC + L2C) for accuracy tracking

              if (confidence_q[i] >= fill_threshold) {
                GHR.pf_issued++; // GHR only counts L2C fills (unchanged; used for global_accuracy)
                if (GHR.pf_issued > GLOBAL_COUNTER_MAX) {
                  GHR.pf_issued >>= 1;
                  GHR.pf_useful >>= 1;
                }
                if constexpr (SPP_DEBUG_PRINT) {
                  std::cout << "[ChampSim] SPP L2 prefetch issued GHR.pf_issued: " << GHR.pf_issued << " GHR.pf_useful: " << GHR.pf_useful << std::endl;
                }
              }

              if constexpr (SPP_DEBUG_PRINT) {
                std::cout << "[ChampSim] " << __func__ << " base_addr: " << base_addr << " pf_addr: " << pf_addr;
                std::cout << " prefetch_delta: " << delta_q[i] << " confidence: " << confidence_q[i];
                std::cout << " depth: " << i << std::endl;
              }
            }
          }
        } else { // Prefetch request is crossing the physical page boundary
          if constexpr (GHR_ON) {
            // Store this prefetch request in GHR to bootstrap SPP learning when
            // we see a ST miss (i.e., accessing a new page)
            GHR.update_entry(curr_sig, confidence_q[i], spp_gsp_tiered::offset_type{pf_addr}, delta_q[i]);
          }
        }

        do_lookahead = 1;
        pf_q_head++;
      }
    }

    // Update base_addr and curr_sig
    if (lookahead_way < PT_WAY) {
      uint32_t set = get_hash(curr_sig) % PT_SET;
      base_addr += (PT.delta[set][lookahead_way] << LOG2_BLOCK_SIZE);

      // PT.delta uses a 7-bit sign magnitude representation to generate
      // sig_delta
      // int sig_delta = (PT.delta[set][lookahead_way] < 0) ? ((((-1) *
      // PT.delta[set][lookahead_way]) & 0x3F) + 0x40) :
      // PT.delta[set][lookahead_way];
      auto sig_delta = (PT.delta[set][lookahead_way] < 0) ? (((-1) * PT.delta[set][lookahead_way]) + (1 << (SIG_DELTA_BIT - 1))) : PT.delta[set][lookahead_way];
      curr_sig = ((curr_sig << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
    }

    if constexpr (SPP_DEBUG_PRINT) {
      std::cout << "Looping curr_sig: " << std::hex << curr_sig << " base_addr: " << base_addr << std::dec;
      std::cout << " pf_q_head: " << pf_q_head << " pf_q_tail: " << pf_q_tail << " depth: " << depth << std::endl;
    }
  } while (LOOKAHEAD_ON && do_lookahead);

  return metadata_in;
}

uint32_t spp_gsp_tiered::prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  if constexpr (FILTER_ON) {
    if constexpr (SPP_DEBUG_PRINT) {
      std::cout << std::endl;
    }
    FILTER.check(evicted_addr, spp_gsp_tiered::L2C_EVICT);
  }

  return metadata_in;
}

void spp_gsp_tiered::prefetcher_final_stats()
{
  finalize_pressure_epoch();

  const double avg_epoch_mshr_util = pressure_epoch_count ? (pressure_epoch_avg_mshr_sum / static_cast<double>(pressure_epoch_count)) : 0.0;
  const double avg_epoch_llc_rq_util = pressure_epoch_count ? (pressure_epoch_avg_llc_rq_sum / static_cast<double>(pressure_epoch_count)) : 0.0;
  const double frac_epoch_avg_mshr_ge_thresh =
      pressure_epoch_count ? (static_cast<double>(pressure_epoch_avg_mshr_above_thresh) / static_cast<double>(pressure_epoch_count)) : 0.0;
  const double frac_epoch_max_mshr_ge_thresh =
      pressure_epoch_count ? (static_cast<double>(pressure_epoch_max_mshr_above_thresh) / static_cast<double>(pressure_epoch_count)) : 0.0;
  const double frac_epoch_avg_llc_rq_ge_thresh =
      pressure_epoch_count ? (static_cast<double>(pressure_epoch_avg_llc_rq_above_thresh) / static_cast<double>(pressure_epoch_count)) : 0.0;
  const double frac_epoch_max_llc_rq_ge_thresh =
      pressure_epoch_count ? (static_cast<double>(pressure_epoch_max_llc_rq_above_thresh) / static_cast<double>(pressure_epoch_count)) : 0.0;

  std::cout << "[" << VARIANT_TAG << "] cpu=" << intern_->cpu
            << " candidate_id=" << params.candidate_id
            << " variant_tag=" << VARIANT_TAG
            << " Final level=" << fdp_level
            << " local_low_accuracy_epochs=" << local_low_accuracy_epochs
            << " local_relief_epochs=" << local_relief_epochs
            << " pf_threshold=" << pf_threshold
            << " fill_threshold=" << fill_threshold
            << " issue_period=" << bwc_issue_period
            << " burst_activation_gap=" << params.burst_activation_gap
            << " low_acc_mode=" << params.low_accuracy_mode_label
            << " low_acc_override_tier=" << params.low_accuracy_override_tier
            << " global_mode_epochs=" << global_mode_epoch_count
            << " global_tier1_epochs=" << global_tier1_epoch_count
            << " global_tier2_epochs=" << global_tier2_epoch_count
            << " global_tier3_epochs=" << global_tier3_epoch_count
            << " max_global_avg_mshr_util=" << max_global_avg_mshr_util
            << " max_global_max_mshr_util=" << max_global_max_mshr_util
            << " max_global_avg_llc_rq_util=" << max_global_avg_llc_rq_util
            << " max_global_max_llc_rq_util=" << max_global_max_llc_rq_util
            << " pressure_mode=" << params.pressure_mode_label
            << std::endl;
  std::cout << "[BWC_PRESSURE] cpu=" << intern_->cpu
            << " epochs=" << pressure_epoch_count
            << " avg_epoch_mshr_util=" << avg_epoch_mshr_util
            << " max_epoch_mshr_util=" << pressure_global_mshr_max
            << " avg_epoch_llc_rq_util=" << avg_epoch_llc_rq_util
            << " max_epoch_llc_rq_util=" << pressure_global_llc_rq_max
            << " frac_epoch_avg_mshr_ge_thresh=" << frac_epoch_avg_mshr_ge_thresh
            << " frac_epoch_max_mshr_ge_thresh=" << frac_epoch_max_mshr_ge_thresh
            << " frac_epoch_avg_llc_rq_ge_thresh=" << frac_epoch_avg_llc_rq_ge_thresh
            << " frac_epoch_max_llc_rq_ge_thresh=" << frac_epoch_max_llc_rq_ge_thresh
            << std::endl;
}

void spp_gsp_tiered::load_runtime_params()
{
  if (params_loaded)
    return;

  maybe_set_double("SPP_GSP_TIERED_ACC_LOW_THROTTLE", params.acc_low_throttle);
  maybe_set_double("SPP_GSP_TIERED_LOCAL_RUNWAY_MSHR", params.local_runway_mshr);
  maybe_set_double("SPP_GSP_TIERED_LOCAL_RUNWAY_LLC_RQ", params.local_runway_llc_rq);
  maybe_set_double("SPP_GSP_TIERED_BURST_ACTIVATION_GAP", params.burst_activation_gap);
  maybe_set_uint("SPP_GSP_TIERED_LOCAL_THROTTLE_PATIENCE", params.local_throttle_patience);
  maybe_set_uint("SPP_GSP_TIERED_LOCAL_RELIEF_PATIENCE", params.local_relief_patience);
  maybe_set_double("SPP_GSP_TIERED_GLOBAL_MSHR_T1", params.global_mshr_t1);
  maybe_set_double("SPP_GSP_TIERED_GLOBAL_MSHR_T2", params.global_mshr_t2);
  maybe_set_double("SPP_GSP_TIERED_GLOBAL_MSHR_T3", params.global_mshr_t3);
  maybe_set_double("SPP_GSP_TIERED_GLOBAL_LLC_T1", params.global_llc_t1);
  maybe_set_double("SPP_GSP_TIERED_GLOBAL_LLC_T2", params.global_llc_t2);
  maybe_set_double("SPP_GSP_TIERED_GLOBAL_LLC_T3", params.global_llc_t3);
  maybe_set_uint("SPP_GSP_TIERED_TIER1_ISSUE_PERIOD", params.tier1_issue_period);
  maybe_set_uint("SPP_GSP_TIERED_TIER2_ISSUE_PERIOD", params.tier2_issue_period);
  maybe_set_uint("SPP_GSP_TIERED_TIER3_PF_THRESHOLD", params.tier3_pf_threshold);
  maybe_set_uint("SPP_GSP_TIERED_MIN_ACTIVE_CORES", params.min_active_cores);
  maybe_set_uint("SPP_GSP_TIERED_CONGESTED_EPOCHS", params.congested_epochs);
  maybe_set_uint("SPP_GSP_TIERED_RELAXED_EPOCHS", params.relaxed_epochs);
  maybe_set_uint("SPP_GSP_TIERED_LOW_ACC_OVERRIDE_TIER", params.low_accuracy_override_tier);
  maybe_set_string("SPP_GSP_TIERED_CANDIDATE_ID", params.candidate_id);
  maybe_set_pressure_mode("SPP_GSP_TIERED_PRESSURE_MODE", params.pressure_mode, params.pressure_mode_label);
  maybe_set_low_accuracy_mode("SPP_GSP_TIERED_LOW_ACC_MODE", params.low_accuracy_mode, params.low_accuracy_mode_label);

  params.global_mshr_t2 = std::max(params.global_mshr_t1, params.global_mshr_t2);
  params.global_mshr_t3 = std::max(params.global_mshr_t2, params.global_mshr_t3);
  params.global_llc_t2 = std::max(params.global_llc_t1, params.global_llc_t2);
  params.global_llc_t3 = std::max(params.global_llc_t2, params.global_llc_t3);
  params.local_runway_mshr = std::clamp(params.local_runway_mshr, 0.0, 1.0);
  params.local_runway_llc_rq = std::clamp(params.local_runway_llc_rq, 0.0, 1.0);
  params.burst_activation_gap = std::clamp(params.burst_activation_gap, 0.0, 1.0);
  params.local_throttle_patience = std::max(1u, params.local_throttle_patience);
  params.local_relief_patience = std::max(1u, params.local_relief_patience);
  params.tier1_issue_period = std::max(1u, params.tier1_issue_period);
  params.tier2_issue_period = std::max(params.tier1_issue_period, params.tier2_issue_period);
  params.tier3_pf_threshold = std::clamp(params.tier3_pf_threshold, 1u, 100u);
  params.min_active_cores = std::max(1u, params.min_active_cores);
  params.congested_epochs = std::max(1u, params.congested_epochs);
  params.relaxed_epochs = std::max(1u, params.relaxed_epochs);
  params.low_accuracy_override_tier = std::clamp(params.low_accuracy_override_tier, 1u, 3u);
  params.acc_low_throttle = std::clamp(params.acc_low_throttle, 0.0, 1.0);

  params_loaded = true;
}

void spp_gsp_tiered::record_pressure_sample()
{
  const double mshr_util = intern_->get_mshr_occupancy_ratio();
  const double llc_rq_util =
      static_cast<double>(intern_->lower_level->rq_occupancy()) /
      static_cast<double>(intern_->lower_level->rq_size());

  ++pressure_epoch_sample_count;
  pressure_epoch_mshr_sum += mshr_util;
  pressure_epoch_llc_rq_sum += llc_rq_util;
  pressure_epoch_mshr_max = std::max(pressure_epoch_mshr_max, mshr_util);
  pressure_epoch_llc_rq_max = std::max(pressure_epoch_llc_rq_max, llc_rq_util);
}

void spp_gsp_tiered::finalize_pressure_epoch()
{
  if (pressure_epoch_sample_count == 0)
    return;

  const double avg_epoch_mshr_util = pressure_epoch_mshr_sum / static_cast<double>(pressure_epoch_sample_count);
  const double avg_epoch_llc_rq_util = pressure_epoch_llc_rq_sum / static_cast<double>(pressure_epoch_sample_count);

  ++pressure_epoch_count;
  pressure_epoch_avg_mshr_sum += avg_epoch_mshr_util;
  pressure_epoch_avg_llc_rq_sum += avg_epoch_llc_rq_util;
  pressure_global_mshr_max = std::max(pressure_global_mshr_max, pressure_epoch_mshr_max);
  pressure_global_llc_rq_max = std::max(pressure_global_llc_rq_max, pressure_epoch_llc_rq_max);

  if (avg_epoch_mshr_util >= params.global_mshr_t1)
    ++pressure_epoch_avg_mshr_above_thresh;
  if (pressure_epoch_mshr_max >= params.global_mshr_t1)
    ++pressure_epoch_max_mshr_above_thresh;
  if (avg_epoch_llc_rq_util >= params.global_llc_t1)
    ++pressure_epoch_avg_llc_rq_above_thresh;
  if (pressure_epoch_llc_rq_max >= params.global_llc_t1)
    ++pressure_epoch_max_llc_rq_above_thresh;

  pressure_epoch_sample_count = 0;
  pressure_epoch_mshr_sum = 0.0;
  pressure_epoch_llc_rq_sum = 0.0;
  pressure_epoch_mshr_max = 0.0;
  pressure_epoch_llc_rq_max = 0.0;
}

bool spp_gsp_tiered::pressure_at_or_above(double avg_value, double max_value, double threshold) const
{
  switch (params.pressure_mode) {
  case pressure_mode_t::avg:
    return avg_value >= threshold;
  case pressure_mode_t::max:
    return max_value >= threshold;
  case pressure_mode_t::avg_or_max:
    return avg_value >= threshold || max_value >= threshold;
  }

  return false;
}

uint32_t spp_gsp_tiered::select_global_tier(double avg_mshr_util, double max_mshr_util, double avg_llc_rq_util, double max_llc_rq_util) const
{
  const bool tier3 =
      pressure_at_or_above(avg_mshr_util, max_mshr_util, params.global_mshr_t3) ||
      pressure_at_or_above(avg_llc_rq_util, max_llc_rq_util, params.global_llc_t3);
  if (tier3)
    return 3;

  const bool tier2 =
      pressure_at_or_above(avg_mshr_util, max_mshr_util, params.global_mshr_t2) ||
      pressure_at_or_above(avg_llc_rq_util, max_llc_rq_util, params.global_llc_t2);
  if (tier2)
    return 2;

  const bool tier1 =
      pressure_at_or_above(avg_mshr_util, max_mshr_util, params.global_mshr_t1) ||
      pressure_at_or_above(avg_llc_rq_util, max_llc_rq_util, params.global_llc_t1);
  if (tier1)
    return 1;

  return 0;
}

bool spp_gsp_tiered::bwc_should_issue()
{
  if (bwc_issue_period <= 1) return true;
  return (++bwc_drop_counter % bwc_issue_period) == 0;
}

void spp_gsp_tiered::bwc_update_epoch()
{
  load_runtime_params();
  if (fdp_epoch_pf_issued == 0)
    return;

  const double mshr_util = intern_->get_mshr_occupancy_ratio();
  const double llc_rq_util =
      static_cast<double>(intern_->lower_level->rq_occupancy()) /
      static_cast<double>(intern_->lower_level->rq_size());
  const double peak_mshr_util = std::max(mshr_util, pressure_epoch_mshr_max);
  const double peak_llc_rq_util = std::max(llc_rq_util, pressure_epoch_llc_rq_max);
  const double accuracy = static_cast<double>(fdp_epoch_pf_useful) /
                          static_cast<double>(fdp_epoch_pf_issued);
  const bool immediate_throttle = (accuracy < params.acc_low_throttle);
  const bool hard_congestion = (peak_llc_rq_util > BWC_THROTTLE_LLC_RQ || peak_mshr_util > BWC_THROTTLE_MSHR);
  const bool immediate_accel = (llc_rq_util < BWC_ACCEL_LLC_RQ && mshr_util < BWC_ACCEL_MSHR && accuracy > FDP_ACC_HIGH);
  const bool runway_open = (mshr_util < params.local_runway_mshr && llc_rq_util < params.local_runway_llc_rq);
  const bool burst_congestion =
      (pressure_epoch_mshr_max - mshr_util >= params.burst_activation_gap) ||
      (pressure_epoch_llc_rq_max - llc_rq_util >= params.burst_activation_gap);

  if (immediate_throttle) {
    ++local_low_accuracy_epochs;
  } else {
    local_low_accuracy_epochs = 0;
  }

  publish_shared_epoch_state(intern_->cpu, accuracy, peak_mshr_util, peak_llc_rq_util);
  const auto shared = snapshot_shared_epoch_state(params.acc_low_throttle);
  max_global_avg_mshr_util = std::max(max_global_avg_mshr_util, shared.avg_mshr_util);
  max_global_max_mshr_util = std::max(max_global_max_mshr_util, shared.max_mshr_util);
  max_global_avg_llc_rq_util = std::max(max_global_avg_llc_rq_util, shared.avg_llc_rq_util);
  max_global_max_llc_rq_util = std::max(max_global_max_llc_rq_util, shared.max_llc_rq_util);

  const bool local_throttle =
      hard_congestion ||
      (immediate_throttle && !runway_open && local_low_accuracy_epochs >= params.local_throttle_patience);

  if (local_throttle) {
    if (fdp_level > 1)
      --fdp_level;
    local_relief_epochs = 0;
  } else if (immediate_accel) {
    ++local_relief_epochs;
    if (local_relief_epochs >= params.local_relief_patience && fdp_level < 5) {
      ++fdp_level;
      local_relief_epochs = 0;
    }
  } else {
    local_relief_epochs = 0;
  }

  pf_threshold     = FDP_PF_THRESH[fdp_level];
  fill_threshold   = FDP_FILL_THRESH[fdp_level];
  bwc_issue_period = BWC_ISSUE_PERIOD[fdp_level];

  const uint32_t pressure_tier = select_global_tier(shared.avg_mshr_util, shared.max_mshr_util, shared.avg_llc_rq_util, shared.max_llc_rq_util);
  const bool low_accuracy_any = shared.low_accuracy_cores > 0;
  const bool low_accuracy_all = shared.active_cores > 0 && shared.low_accuracy_cores == shared.active_cores;
  bool global_mode_eligible = shared.active_cores >= params.min_active_cores;
  switch (params.low_accuracy_mode) {
  case low_accuracy_mode_t::any:
    global_mode_eligible = global_mode_eligible && !low_accuracy_any;
    break;
  case low_accuracy_mode_t::all:
    global_mode_eligible = global_mode_eligible && !low_accuracy_all;
    break;
  case low_accuracy_mode_t::pressure_override:
    global_mode_eligible = global_mode_eligible && (!low_accuracy_any || pressure_tier >= params.low_accuracy_override_tier);
    break;
  }
  const uint32_t observed_tier = global_mode_eligible ? pressure_tier : 0;
  const bool relaxed_epoch =
      (observed_tier == 0) &&
      (accuracy > BWC_RELIEF_ACC) &&
      (shared.avg_mshr_util < params.global_mshr_t1) &&
      (shared.avg_llc_rq_util < params.global_llc_t1);
  const bool bursty_global_pressure =
      observed_tier > 0 &&
      (shared.max_mshr_util - shared.avg_mshr_util >= params.burst_activation_gap ||
       shared.max_llc_rq_util - shared.avg_llc_rq_util >= params.burst_activation_gap);
  const uint32_t previous_active_global_tier = active_global_tier;

  if (observed_tier > 0) {
    if (bursty_global_pressure && observed_tier > active_global_tier) {
      active_global_tier = observed_tier;
      global_congested_epochs = 0;
      global_relaxed_epochs = 0;
    } else {
      ++global_congested_epochs;
      global_relaxed_epochs = 0;
    }
  } else if (relaxed_epoch) {
    ++global_relaxed_epochs;
    global_congested_epochs = 0;
  } else {
    global_congested_epochs = 0;
    global_relaxed_epochs = 0;
  }

  if (observed_tier > 0 && global_congested_epochs >= params.congested_epochs) {
    active_global_tier = observed_tier;
  } else if (observed_tier == 0 && global_relaxed_epochs >= params.relaxed_epochs) {
    active_global_tier = 0;
  }

  if (active_global_tier > 0) {
    ++global_mode_epoch_count;

    if (active_global_tier >= 1) {
      ++global_tier1_epoch_count;
      bwc_issue_period = std::max(bwc_issue_period, params.tier1_issue_period);
    }
    if (active_global_tier >= 2) {
      ++global_tier2_epoch_count;
      bwc_issue_period = std::max(bwc_issue_period, params.tier2_issue_period);
    }
    if (active_global_tier >= 3) {
      ++global_tier3_epoch_count;
      pf_threshold = std::max(pf_threshold, params.tier3_pf_threshold);
    }
  }

  if (active_global_tier != previous_active_global_tier || burst_congestion)
    bwc_drop_counter = 0;

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << "[" << VARIANT_TAG << "] epoch: issued=" << fdp_epoch_pf_issued
              << " useful=" << fdp_epoch_pf_useful
              << " acc=" << accuracy
              << " mshr_util=" << mshr_util
              << " llc_rq_util=" << llc_rq_util
              << " peak_mshr_util=" << peak_mshr_util
              << " peak_llc_rq_util=" << peak_llc_rq_util
              << " global_avg_mshr_util=" << shared.avg_mshr_util
              << " global_max_mshr_util=" << shared.max_mshr_util
              << " global_avg_llc_rq_util=" << shared.avg_llc_rq_util
              << " global_max_llc_rq_util=" << shared.max_llc_rq_util
              << " low_accuracy_cores=" << shared.low_accuracy_cores
              << " burst_congestion=" << burst_congestion
              << " bursty_global_pressure=" << bursty_global_pressure
              << " observed_tier=" << observed_tier
              << " active_tier=" << active_global_tier
              << " congested_epochs=" << global_congested_epochs
              << " relaxed_epochs=" << global_relaxed_epochs
              << " level=" << fdp_level
              << " pf_thresh=" << pf_threshold
              << " issue_period=" << bwc_issue_period << "\n";
  }
}

// TODO: Find a good 64-bit hash function
uint64_t spp_gsp_tiered::get_hash(uint64_t key)
{
  // Robert Jenkins' 32 bit mix function
  key += (key << 12);
  key ^= (key >> 22);
  key += (key << 4);
  key ^= (key >> 9);
  key += (key << 10);
  key ^= (key >> 2);
  key += (key << 7);
  key ^= (key >> 12);

  // Knuth's multiplicative method
  key = (key >> 3) * 2654435761;

  return key;
}

void spp_gsp_tiered::SIGNATURE_TABLE::read_and_update_sig(champsim::address addr, uint32_t& last_sig, uint32_t& curr_sig, typename offset_type::difference_type& delta)
{
  auto set = get_hash(champsim::page_number{addr}.to<uint64_t>()) % ST_SET;
  auto match = ST_WAY;
  tag_type partial_page{addr};
  offset_type page_offset{addr};
  uint8_t ST_hit = 0;
  long sig_delta = 0;

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << "[ST] " << __func__ << " page: " << champsim::page_number{addr} << " partial_page: " << std::hex << partial_page << std::dec << std::endl;
  }

  // Case 2: Invalid
  if (match == ST_WAY) {
    for (match = 0; match < ST_WAY; match++) {
      if (valid[set][match] && (tag[set][match] == partial_page)) {
        last_sig = sig[set][match];
        delta = champsim::offset(last_offset[set][match], page_offset);

        if (delta) {
          // Build a new sig based on 7-bit sign magnitude representation of delta
          // sig_delta = (delta < 0) ? ((((-1) * delta) & 0x3F) + 0x40) : delta;
          sig_delta = (delta < 0) ? (((-1) * delta) + (1 << (SIG_DELTA_BIT - 1))) : delta;
          sig[set][match] = ((last_sig << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
          curr_sig = sig[set][match];
          last_offset[set][match] = page_offset;

          if constexpr (SPP_DEBUG_PRINT) {
            std::cout << "[ST] " << __func__ << " hit set: " << set << " way: " << match;
            std::cout << " valid: " << valid[set][match] << " tag: " << std::hex << tag[set][match];
            std::cout << " last_sig: " << last_sig << " curr_sig: " << curr_sig;
            std::cout << " delta: " << std::dec << delta << " last_offset: " << page_offset << std::endl;
          }
        } else
          last_sig = 0; // Hitting the same cache line, delta is zero

        ST_hit = 1;
        break;
      }
    }
  }

  // Case 2: Invalid
  if (match == ST_WAY) {
    for (match = 0; match < ST_WAY; match++) {
      if (valid[set][match] == 0) {
        valid[set][match] = 1;
        tag[set][match] = partial_page;
        sig[set][match] = 0;
        curr_sig = sig[set][match];
        last_offset[set][match] = page_offset;

        if constexpr (SPP_DEBUG_PRINT) {
          std::cout << "[ST] " << __func__ << " invalid set: " << set << " way: " << match;
          std::cout << " valid: " << valid[set][match] << " tag: " << std::hex << partial_page;
          std::cout << " sig: " << sig[set][match] << " last_offset: " << std::dec << page_offset << std::endl;
        }

        break;
      }
    }
  }

  if constexpr (SPP_SANITY_CHECK) {
    // Assertion
    if (match == ST_WAY) {
      for (match = 0; match < ST_WAY; match++) {
        if (lru[set][match] == ST_WAY - 1) { // Find replacement victim
          tag[set][match] = partial_page;
          sig[set][match] = 0;
          curr_sig = sig[set][match];
          last_offset[set][match] = page_offset;

          if constexpr (SPP_DEBUG_PRINT) {
            std::cout << "[ST] " << __func__ << " miss set: " << set << " way: " << match;
            std::cout << " valid: " << valid[set][match] << " victim tag: " << std::hex << tag[set][match] << " new tag: " << partial_page;
            std::cout << " sig: " << sig[set][match] << " last_offset: " << std::dec << page_offset << std::endl;
          }

          break;
        }
      }

      // Assertion
      if (match == ST_WAY) {
        std::cout << "[ST] Cannot find a replacement victim!" << std::endl;
        assert(0);
      }
    }
  }

  if constexpr (GHR_ON) {
    if (ST_hit == 0) {
      uint32_t GHR_found = _parent->GHR.check_entry(page_offset);
      if (GHR_found < MAX_GHR_ENTRY) {
        sig_delta = (_parent->GHR.delta[GHR_found] < 0) ? (((-1) * _parent->GHR.delta[GHR_found]) + (1 << (SIG_DELTA_BIT - 1))) : _parent->GHR.delta[GHR_found];
        sig[set][match] = ((_parent->GHR.sig[GHR_found] << SIG_SHIFT) ^ sig_delta) & SIG_MASK;
        curr_sig = sig[set][match];
      }
    }
  }

  // Update LRU
  for (uint32_t way = 0; way < ST_WAY; way++) {
    if (lru[set][way] < lru[set][match]) {
      lru[set][way]++;

      if constexpr (SPP_SANITY_CHECK) {
        // Assertion
        if (lru[set][way] >= ST_WAY) {
          std::cout << "[ST] LRU value is wrong! set: " << set << " way: " << way << " lru: " << lru[set][way] << std::endl;
          assert(0);
        }
      }
    }
  }

  lru[set][match] = 0; // Promote to the MRU position
}

void spp_gsp_tiered::PATTERN_TABLE::update_pattern(uint32_t last_sig, typename offset_type::difference_type curr_delta)
{
  // Update (sig, delta) correlation
  uint32_t set = get_hash(last_sig) % PT_SET, match = 0;

  // Case 1: Hit
  for (match = 0; match < PT_WAY; match++) {
    if (delta[set][match] == curr_delta) {
      c_delta[set][match]++;
      c_sig[set]++;
      if (c_sig[set] > C_SIG_MAX) {
        for (uint32_t way = 0; way < PT_WAY; way++)
          c_delta[set][way] >>= 1;
        c_sig[set] >>= 1;
      }

      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[PT] " << __func__ << " hit sig: " << std::hex << last_sig << std::dec << " set: " << set << " way: " << match;
        std::cout << " delta: " << delta[set][match] << " c_delta: " << c_delta[set][match] << " c_sig: " << c_sig[set] << std::endl;
      }

      break;
    }
  }

  // Case 2: Miss
  if (match == PT_WAY) {
    uint32_t victim_way = PT_WAY, min_counter = C_SIG_MAX;

    for (match = 0; match < PT_WAY; match++) {
      if (c_delta[set][match] < min_counter) { // Select an entry with the minimum c_delta
        victim_way = match;
        min_counter = c_delta[set][match];
      }
    }

    delta[set][victim_way] = curr_delta;
    c_delta[set][victim_way] = 0;
    c_sig[set]++;
    if (c_sig[set] > C_SIG_MAX) {
      for (uint32_t way = 0; way < PT_WAY; way++)
        c_delta[set][way] >>= 1;
      c_sig[set] >>= 1;
    }

    if constexpr (SPP_DEBUG_PRINT) {
      std::cout << "[PT] " << __func__ << " miss sig: " << std::hex << last_sig << std::dec << " set: " << set << " way: " << victim_way;
      std::cout << " delta: " << delta[set][victim_way] << " c_delta: " << c_delta[set][victim_way] << " c_sig: " << c_sig[set] << std::endl;
    }

    if constexpr (SPP_SANITY_CHECK) {
      // Assertion
      if (victim_way == PT_WAY) {
        std::cout << "[PT] Cannot find a replacement victim!" << std::endl;
        assert(0);
      }
    }
  }
}

void spp_gsp_tiered::PATTERN_TABLE::read_pattern(uint32_t curr_sig, std::vector<typename offset_type::difference_type>& delta_q, std::vector<uint32_t>& confidence_q,
                                          uint32_t& lookahead_way, uint32_t& lookahead_conf, uint32_t& pf_q_tail, uint32_t& depth,
                                          uint32_t pf_thresh)
{
  // Update (sig, delta) correlation
  uint32_t set = get_hash(curr_sig) % PT_SET, local_conf = 0, pf_conf = 0, max_conf = 0;

  if (c_sig[set]) {
    for (uint32_t way = 0; way < PT_WAY; way++) {
      local_conf = (100 * c_delta[set][way]) / c_sig[set];
      pf_conf = depth ? (_parent->GHR.global_accuracy * c_delta[set][way] / c_sig[set] * lookahead_conf / 100) : local_conf;

      if (pf_conf >= pf_thresh) {
        confidence_q[pf_q_tail] = pf_conf;
        delta_q[pf_q_tail] = delta[set][way];

        // Lookahead path follows the most confident entry
        if (pf_conf > max_conf) {
          lookahead_way = way;
          max_conf = pf_conf;
        }
        pf_q_tail++;

        if constexpr (SPP_DEBUG_PRINT) {
          std::cout << "[PT] " << __func__ << " HIGH CONF: " << pf_conf << " sig: " << std::hex << curr_sig << std::dec << " set: " << set << " way: " << way;
          std::cout << " delta: " << delta[set][way] << " c_delta: " << c_delta[set][way] << " c_sig: " << c_sig[set];
          std::cout << " conf: " << local_conf << " depth: " << depth << std::endl;
        }
      } else {
        if constexpr (SPP_DEBUG_PRINT) {
          std::cout << "[PT] " << __func__ << "  LOW CONF: " << pf_conf << " sig: " << std::hex << curr_sig << std::dec << " set: " << set << " way: " << way;
          std::cout << " delta: " << delta[set][way] << " c_delta: " << c_delta[set][way] << " c_sig: " << c_sig[set];
          std::cout << " conf: " << local_conf << " depth: " << depth << std::endl;
        }
      }
    }
    pf_q_tail++;

    lookahead_conf = max_conf;
    if (lookahead_conf >= pf_thresh)
      depth++;

    if constexpr (SPP_DEBUG_PRINT) {
      std::cout << "global_accuracy: " << _parent->GHR.global_accuracy << " lookahead_conf: " << lookahead_conf << std::endl;
    }
  } else {
    confidence_q[pf_q_tail] = 0;
  }
}

bool spp_gsp_tiered::PREFETCH_FILTER::check(champsim::address check_addr, FILTER_REQUEST filter_request)
{
  champsim::block_number cache_line{check_addr};
  auto hash = get_hash(cache_line.to<uint64_t>());
  auto quotient = (hash >> REMAINDER_BIT) & ((1 << QUOTIENT_BIT) - 1);
  auto remainder = hash % (1 << REMAINDER_BIT);

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << "[FILTER] check_addr: " << check_addr << " hash: " << hash << " quotient: " << quotient << " remainder: " << remainder << std::endl;
  }

  switch (filter_request) {
  case spp_gsp_tiered::SPP_L2C_PREFETCH:
    if ((valid[quotient] || useful[quotient]) && remainder_tag[quotient] == remainder) {
      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[FILTER] " << __func__ << " line is already in the filter check_addr: " << check_addr << " cache_line: " << cache_line;
        std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl;
      }

      return false; // False return indicates "Do not prefetch"
    } else {
      valid[quotient] = 1;  // Mark as prefetched
      useful[quotient] = 0; // Reset useful bit
      remainder_tag[quotient] = remainder;

      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[FILTER] " << __func__ << " set valid for check_addr: " << check_addr << " cache_line: " << cache_line;
        std::cout << " quotient: " << quotient << " remainder_tag: " << remainder_tag[quotient] << " valid: " << valid[quotient]
                  << " useful: " << useful[quotient] << std::endl;
      }
    }
    break;

  case spp_gsp_tiered::SPP_LLC_PREFETCH:
    if ((valid[quotient] || useful[quotient]) && remainder_tag[quotient] == remainder) {
      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[FILTER] " << __func__ << " line is already in the filter check_addr: " << check_addr << " cache_line: " << cache_line;
        std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl;
      }

      return false; // False return indicates "Do not prefetch"
    } else {
      // NOTE: SPP_LLC_PREFETCH has relatively low confidence (FILL_THRESHOLD <= SPP_LLC_PREFETCH < PF_THRESHOLD)
      // Therefore, it is safe to prefetch this cache line in the large LLC and save precious L2C capacity
      // If this prefetch request becomes more confident and SPP eventually issues SPP_L2C_PREFETCH,
      // we can get this cache line immediately from the LLC (not from DRAM)
      // To allow this fast prefetch from LLC, SPP does not set the valid bit for SPP_LLC_PREFETCH

      // valid[quotient] = 1;
      // useful[quotient] = 0;

      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[FILTER] " << __func__ << " don't set valid for check_addr: " << check_addr << " cache_line: " << cache_line;
        std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient] << std::endl;
      }
    }
    break;

  case spp_gsp_tiered::L2C_DEMAND:
    if ((remainder_tag[quotient] == remainder) && (useful[quotient] == 0)) {
      useful[quotient] = 1;
      if (valid[quotient]) {
        _parent->GHR.pf_useful++; // This cache line was prefetched by SPP and actually used in the program
        ++_parent->fdp_epoch_pf_useful; // BWC epoch counter: tracks useful prefetches this epoch
      }

      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[FILTER] " << __func__ << " set useful for check_addr: " << check_addr << " cache_line: " << cache_line;
        std::cout << " quotient: " << quotient << " valid: " << valid[quotient] << " useful: " << useful[quotient];
        std::cout << " GHR.pf_issued: " << _parent->GHR.pf_issued << " GHR.pf_useful: " << _parent->GHR.pf_useful << std::endl;
      }
    }
    break;

  case spp_gsp_tiered::L2C_EVICT:
    // Decrease global pf_useful counter when there is a useless prefetch (prefetched but not used)
    if (valid[quotient] && !useful[quotient] && _parent->GHR.pf_useful)
      _parent->GHR.pf_useful--;

    // Reset filter entry
    valid[quotient] = 0;
    useful[quotient] = 0;
    remainder_tag[quotient] = 0;
    break;

  default:
    // Assertion
    std::cout << "[FILTER] Invalid filter request type: " << filter_request << std::endl;
    assert(0);
  }

  return true;
}

void spp_gsp_tiered::GLOBAL_REGISTER::update_entry(uint32_t pf_sig, uint32_t pf_confidence, offset_type pf_offset, typename offset_type::difference_type pf_delta)
{
  // NOTE: GHR implementation is slightly different from the original paper
  // Instead of matching (last_offset + delta), GHR simply stores and matches the pf_offset
  uint32_t min_conf = UINT32_MAX, victim_way = MAX_GHR_ENTRY;

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << "[GHR] Crossing the page boundary pf_sig: " << std::hex << pf_sig << std::dec;
    std::cout << " confidence: " << pf_confidence << " pf_offset: " << pf_offset << " pf_delta: " << pf_delta << std::endl;
  }

  for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
    // if (sig[i] == pf_sig) { // TODO: Which one is better and consistent?
    //  If GHR already holds the same pf_sig, update the GHR entry with the latest info
    if (valid[i] && (offset[i] == pf_offset)) {
      // If GHR already holds the same pf_offset, update the GHR entry with the latest info
      sig[i] = pf_sig;
      confidence[i] = pf_confidence;
      // offset[i] = pf_offset;
      delta[i] = pf_delta;

      if constexpr (SPP_DEBUG_PRINT) {
        std::cout << "[GHR] Found a matching index: " << i << std::endl;
      }

      return;
    }

    if (!valid[i] && victim_way >= MAX_GHR_ENTRY) {
      victim_way = i;
      continue;
    }

    // GHR replacement policy is based on the stored confidence value
    // An entry with the lowest confidence is selected as a victim
    if (valid[i] && confidence[i] < min_conf) {
      min_conf = confidence[i];
      victim_way = i;
    }
  }

  // Assertion
  if (victim_way >= MAX_GHR_ENTRY) {
    std::cout << "[GHR] Cannot find a replacement victim!" << std::endl;
    assert(0);
  }

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << "[GHR] Replace index: " << victim_way << " pf_sig: " << std::hex << sig[victim_way] << std::dec;
    std::cout << " confidence: " << confidence[victim_way] << " pf_offset: " << offset[victim_way] << " pf_delta: " << delta[victim_way] << std::endl;
  }

  valid[victim_way] = 1;
  sig[victim_way] = pf_sig;
  confidence[victim_way] = pf_confidence;
  offset[victim_way] = pf_offset;
  delta[victim_way] = pf_delta;
}

uint32_t spp_gsp_tiered::GLOBAL_REGISTER::check_entry(offset_type page_offset)
{
  uint32_t max_conf = 0, max_conf_way = MAX_GHR_ENTRY;

  for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
    if ((offset[i] == page_offset) && (max_conf < confidence[i])) {
      max_conf = confidence[i];
      max_conf_way = i;
    }
  }

  return max_conf_way;
}
