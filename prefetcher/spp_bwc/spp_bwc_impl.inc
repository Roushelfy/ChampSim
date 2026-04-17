#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>

namespace
{
constexpr std::size_t BWC_MAX_TRACKED_CPUS = 16;

std::array<bool, BWC_MAX_TRACKED_CPUS> shared_cpu_active{};
std::array<double, BWC_MAX_TRACKED_CPUS> shared_last_accuracy{};
std::array<double, BWC_MAX_TRACKED_CPUS> shared_last_avg_mshr_util{};
std::array<double, BWC_MAX_TRACKED_CPUS> shared_last_max_mshr_util{};
std::array<double, BWC_MAX_TRACKED_CPUS> shared_last_avg_llc_rq_util{};
std::array<double, BWC_MAX_TRACKED_CPUS> shared_last_max_llc_rq_util{};
std::array<bool, BWC_MAX_TRACKED_CPUS> shared_coord_streaming_active{};
std::array<uint64_t, BWC_MAX_TRACKED_CPUS> shared_coord_window_id{};
std::array<uint64_t, BWC_MAX_TRACKED_CPUS> shared_coord_issued{};
std::array<uint64_t, BWC_MAX_TRACKED_CPUS> shared_coord_useful{};
std::array<uint64_t, BWC_MAX_TRACKED_CPUS> shared_coord_head_useful{};
std::array<uint64_t, BWC_MAX_TRACKED_CPUS> shared_coord_timely_useful{};
std::array<uint64_t, BWC_MAX_TRACKED_CPUS> shared_coord_late_useful{};
std::array<uint64_t, BWC_MAX_TRACKED_CPUS> shared_coord_miss_latency_delta{};

struct shared_bwc_snapshot {
  std::size_t active_cores = 0;
  bool any_low_accuracy = false;
  double avg_mshr_util = 0.0;
  double max_mshr_util = 0.0;
  double avg_llc_rq_util = 0.0;
  double max_llc_rq_util = 0.0;
};

struct shared_coord_snapshot {
  std::size_t active_cores = 0;
  int best_core = -1;
  int phase_core = -1;
  uint64_t max_window_id = 0;
};

void publish_shared_epoch_state(std::size_t cpu, double accuracy, double avg_mshr_util, double max_mshr_util, double avg_llc_rq_util, double max_llc_rq_util)
{
  if (cpu >= BWC_MAX_TRACKED_CPUS)
    return;

  shared_cpu_active[cpu] = true;
  shared_last_accuracy[cpu] = accuracy;
  shared_last_avg_mshr_util[cpu] = avg_mshr_util;
  shared_last_max_mshr_util[cpu] = max_mshr_util;
  shared_last_avg_llc_rq_util[cpu] = avg_llc_rq_util;
  shared_last_max_llc_rq_util[cpu] = max_llc_rq_util;
}

shared_bwc_snapshot snapshot_shared_epoch_state(double low_accuracy_threshold)
{
  shared_bwc_snapshot snapshot;

  for (std::size_t cpu = 0; cpu < BWC_MAX_TRACKED_CPUS; ++cpu) {
    if (!shared_cpu_active[cpu])
      continue;

    ++snapshot.active_cores;
    snapshot.avg_mshr_util += shared_last_avg_mshr_util[cpu];
    snapshot.avg_llc_rq_util += shared_last_avg_llc_rq_util[cpu];
    snapshot.max_mshr_util = std::max(snapshot.max_mshr_util, shared_last_max_mshr_util[cpu]);
    snapshot.max_llc_rq_util = std::max(snapshot.max_llc_rq_util, shared_last_max_llc_rq_util[cpu]);
    snapshot.any_low_accuracy = snapshot.any_low_accuracy || (shared_last_accuracy[cpu] < low_accuracy_threshold);
  }

  if (snapshot.active_cores > 0) {
    snapshot.avg_mshr_util /= static_cast<double>(snapshot.active_cores);
    snapshot.avg_llc_rq_util /= static_cast<double>(snapshot.active_cores);
  }

  return snapshot;
}

void publish_shared_coord_state(std::size_t cpu, bool streaming_active, uint64_t window_id, uint64_t issued, uint64_t useful, uint64_t head_useful,
                                uint64_t timely_useful, uint64_t late_useful, uint64_t miss_latency_delta)
{
  if (cpu >= BWC_MAX_TRACKED_CPUS)
    return;

  shared_coord_streaming_active[cpu] = streaming_active;
  shared_coord_window_id[cpu] = window_id;
  shared_coord_issued[cpu] = issued;
  shared_coord_useful[cpu] = useful;
  shared_coord_head_useful[cpu] = head_useful;
  shared_coord_timely_useful[cpu] = timely_useful;
  shared_coord_late_useful[cpu] = late_useful;
  shared_coord_miss_latency_delta[cpu] = miss_latency_delta;
}

shared_coord_snapshot snapshot_shared_coord_state()
{
  shared_coord_snapshot snapshot;
  std::array<int, BWC_MAX_TRACKED_CPUS> active_cpu_ids{};

  for (std::size_t cpu = 0; cpu < BWC_MAX_TRACKED_CPUS; ++cpu) {
    if (!shared_cpu_active[cpu] || !shared_coord_streaming_active[cpu])
      continue;

    active_cpu_ids[snapshot.active_cores++] = static_cast<int>(cpu);
    snapshot.max_window_id = std::max(snapshot.max_window_id, shared_coord_window_id[cpu]);

    const double score = (8.0 * static_cast<double>(shared_coord_timely_useful[cpu])) +
                         (4.0 * static_cast<double>(shared_coord_head_useful[cpu])) +
                         static_cast<double>(shared_coord_useful[cpu]) -
                         (2.0 * static_cast<double>(shared_coord_late_useful[cpu])) -
                         (static_cast<double>(shared_coord_issued[cpu]) / 32.0) -
                         (static_cast<double>(shared_coord_miss_latency_delta[cpu]) / 5000.0);

    if (snapshot.best_core < 0) {
      snapshot.best_core = static_cast<int>(cpu);
    } else {
      const auto prev = static_cast<std::size_t>(snapshot.best_core);
      const double prev_score = (8.0 * static_cast<double>(shared_coord_timely_useful[prev])) +
                                (4.0 * static_cast<double>(shared_coord_head_useful[prev])) +
                                static_cast<double>(shared_coord_useful[prev]) -
                                (2.0 * static_cast<double>(shared_coord_late_useful[prev])) -
                                (static_cast<double>(shared_coord_issued[prev]) / 32.0) -
                                (static_cast<double>(shared_coord_miss_latency_delta[prev]) / 5000.0);
      if ((score > prev_score) || ((score == prev_score) && (shared_coord_window_id[cpu] > shared_coord_window_id[prev])) ||
          ((score == prev_score) && (shared_coord_window_id[cpu] == shared_coord_window_id[prev]) && (cpu < prev))) {
        snapshot.best_core = static_cast<int>(cpu);
      }
    }
  }

  if (snapshot.active_cores > 0) {
    snapshot.phase_core = active_cpu_ids[snapshot.max_window_id % snapshot.active_cores];
    if (snapshot.best_core < 0) {
      snapshot.best_core = snapshot.phase_core;
    }
  }

  return snapshot;
}
} // namespace

void spp_bwc::prefetcher_initialize()
{
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
  coord_last_miss_latency_cycles = static_cast<uint64_t>(intern_->sim_stats.total_miss_latency_cycles);
}

void spp_bwc::prefetcher_cycle_operate() {}

uint32_t spp_bwc::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                           uint32_t metadata_in)
{
  // ── BWC: epoch trigger ────────────────────────────────────────────────────
  record_pressure_sample();
  ++coord_accesses_in_mini_epoch;
  if (coord_accesses_in_mini_epoch >= SPP_BWC_COORD_MINI_EPOCH_ACCESSES) {
    coord_finalize_mini_epoch();
    coord_accesses_in_mini_epoch = 0;
  }
  ++fdp_access_count;
  if (fdp_access_count >= FDP_EPOCH_SIZE) {
    finalize_pressure_epoch();
    bwc_update_epoch();
    fdp_access_count    = 0;
    fdp_epoch_pf_issued = 0;
    fdp_epoch_pf_useful = 0;
  }
  // ─────────────────────────────────────────────────────────────────────────

  if (useful_prefetch) {
    coord_record_useful(metadata_in);
  }

  champsim::page_number page{addr};
  uint32_t last_sig = 0, curr_sig = 0, depth = 0;
  std::vector<uint32_t> confidence_q(intern_->MSHR_SIZE);

  typename spp_bwc::offset_type::difference_type delta = 0;
  std::vector<typename spp_bwc::offset_type::difference_type> delta_q(intern_->MSHR_SIZE);

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
  FILTER.check(addr, spp_bwc::L2C_DEMAND);

  // Stage 2: Update delta patterns stored in PT
  if (last_sig)
    PT.update_pattern(last_sig, delta);

  // Stage 3: Start prefetching
  auto base_addr = addr;
  uint32_t lookahead_conf = 100, pf_q_head = 0, pf_q_tail = 0;
  uint8_t do_lookahead = 0;
  uint32_t same_page_candidate_rank = 0;

  do {
    uint32_t lookahead_way = PT_WAY;
    PT.read_pattern(curr_sig, delta_q, confidence_q, lookahead_way, lookahead_conf, pf_q_tail, depth, pf_threshold);

    do_lookahead = 0;
    for (uint32_t i = pf_q_head; i < pf_q_tail; i++) {
      if (confidence_q[i] >= pf_threshold) {
        champsim::address pf_addr{champsim::block_number{base_addr} + delta_q[i]};

        if (champsim::page_number{pf_addr} == page) { // Prefetch request is in the same physical page
          ++same_page_candidate_rank;
          const uint8_t rank_bucket = utility_rank_bucket(same_page_candidate_rank);
          const bool fill_l2 = confidence_q[i] >= fill_threshold;
          bool issue_fill_l2 = fill_l2;
          uint8_t utility_bucket = SIG_UTIL_BUCKET_HEAD;
          if constexpr (SPP_BWC_ENABLE_HEADGATE) {
            utility_bucket = rank_bucket;
          }

          // BWC rate limiter: gate BEFORE FILTER.check() (which is a combined check-and-set).
          // Skipping after FILTER.check() would mark the address as prefetched without issuing.
          bool allow_prefetch = true;
          if constexpr (SPP_BWC_ENABLE_HEADGATE) {
            if (utility_filter_enabled) {
              if (same_page_candidate_rank <= headgate_keep_candidates_current) {
                allow_prefetch = true;
              } else if constexpr (SPP_BWC_ENABLE_HEADGATE_L2QUAL) {
                allow_prefetch = true;
                if (issue_fill_l2) {
                  issue_fill_l2 = utility_allows_l2_fill(curr_sig, utility_bucket);
                }
              } else if constexpr (SPP_BWC_ENABLE_HEADGATE_TAIL_DEMOTE) {
                if (rank_bucket == SIG_UTIL_BUCKET_TAIL) {
                  allow_prefetch = true;
                  issue_fill_l2 = false;
                  ++headgate_tail_demoted;
                } else {
                  const bool utility_gate_candidate = (rank_bucket == SIG_UTIL_BUCKET_MID);
                  if (utility_gate_candidate) {
                    allow_prefetch = utility_allows_prefetch(curr_sig, utility_bucket);
                    if (!allow_prefetch) {
                      ++utility_prefetch_filtered;
                      ++headgate_mid_filtered;
                    }
                  } else {
                    allow_prefetch = false;
                    ++utility_prefetch_filtered;
                    ++headgate_tail_filtered;
                  }
                }
              } else {
                bool tail_passthrough = false;
                bool utility_gate_candidate = (rank_bucket == SIG_UTIL_BUCKET_MID);
                bool tail_confgate_candidate = false;
                if constexpr (SPP_BWC_ENABLE_HEADGATE_STREAMING_KEEP_TAIL || SPP_BWC_ENABLE_HEADGATE_PARITYBOOST ||
                              SPP_BWC_ENABLE_HEADGATE_STAGGERPF || SPP_BWC_ENABLE_HEADGATE_STAGGERFILL ||
                              SPP_BWC_ENABLE_HEADGATE_DYNBOOST || SPP_BWC_ENABLE_HEADGATE_DYNRUNWAY ||
                              SPP_BWC_ENABLE_HEADGATE_PHASEROTATE) {
                  tail_passthrough = headgate_streaming_keep_tail_active && (rank_bucket == SIG_UTIL_BUCKET_TAIL);
                }
                if constexpr (SPP_BWC_ENABLE_HEADGATE_CRIT_CONFGATE) {
                  tail_confgate_candidate = (rank_bucket == SIG_UTIL_BUCKET_TAIL);
                }
                if constexpr (SPP_BWC_ENABLE_HEADGATE_TAIL_UTILITY) {
                  utility_gate_candidate = utility_gate_candidate || (rank_bucket == SIG_UTIL_BUCKET_TAIL);
                }

                if (tail_passthrough) {
                  allow_prefetch = true;
                } else if (utility_gate_candidate || tail_confgate_candidate) {
                  allow_prefetch = utility_allows_prefetch(curr_sig, utility_bucket);
                  if constexpr (SPP_BWC_ENABLE_HEADGATE_CRIT_CONFGATE) {
                    if (allow_prefetch && rank_bucket == SIG_UTIL_BUCKET_MID && confidence_q[i] < headgate_crit_mid_conf_threshold_current) {
                      allow_prefetch = false;
                      ++headgate_crit_mid_conf_filtered;
                    } else if (allow_prefetch && rank_bucket == SIG_UTIL_BUCKET_TAIL) {
                      if (confidence_q[i] < headgate_crit_tail_conf_threshold_current) {
                        allow_prefetch = false;
                        ++headgate_crit_tail_conf_filtered;
                      } else {
                        ++headgate_crit_tail_conf_rescued;
                        if (issue_fill_l2 && confidence_q[i] < headgate_crit_tail_fill_conf_threshold_current) {
                          issue_fill_l2 = false;
                          ++headgate_crit_tail_fill_demoted;
                        }
                      }
                    }
                  }
                  if (!allow_prefetch) {
                    ++utility_prefetch_filtered;
                    if (rank_bucket == SIG_UTIL_BUCKET_MID) {
                      ++headgate_mid_filtered;
                    } else {
                      ++headgate_tail_filtered;
                    }
                  }
                } else {
                  allow_prefetch = false;
                  ++utility_prefetch_filtered;
                  ++headgate_tail_filtered;
                }
              }
            }
          } else if (fill_l2 && !utility_allows_prefetch(curr_sig, utility_bucket)) {
            allow_prefetch = false;
            ++utility_prefetch_filtered;
          }

          if (allow_prefetch && bwc_should_issue()) {
            if (FILTER.check(pf_addr, (issue_fill_l2 ? spp_bwc::SPP_L2C_PREFETCH : spp_bwc::SPP_LLC_PREFETCH))) {
              const uint32_t prefetch_metadata =
                  encode_prefetch_metadata(static_cast<uint8_t>(intern_->cpu), utility_bucket, coord_issue_tag_current);
              prefetch_line(pf_addr, issue_fill_l2, prefetch_metadata); // Use addr (not base_addr) to obey the same physical page boundary

              ++fdp_epoch_pf_issued; // BWC: count ALL issued prefetches (LLC + L2C) for accuracy tracking
              coord_record_issued(utility_bucket);

              if (issue_fill_l2) {
                utility_record_prefetch(pf_addr, curr_sig, utility_bucket, prefetch_metadata);
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
            GHR.update_entry(curr_sig, confidence_q[i], spp_bwc::offset_type{pf_addr}, delta_q[i]);
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

uint32_t spp_bwc::prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  if constexpr (FILTER_ON) {
    if constexpr (SPP_DEBUG_PRINT) {
      std::cout << std::endl;
    }
    FILTER.check(evicted_addr, spp_bwc::L2C_EVICT);
  }

  if (prefetch) {
    coord_record_fill(metadata_in, prefetch);
  }

  return metadata_in;
}

void spp_bwc::prefetcher_final_stats()
{
  if (coord_accesses_in_mini_epoch > 0) {
    coord_finalize_mini_epoch(true);
    coord_accesses_in_mini_epoch = 0;
  }
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

  std::cout << "[" << SPP_BWC_VARIANT_TAG << "] cpu=" << intern_->cpu
            << " Final level=" << fdp_level
            << " pf_threshold=" << pf_threshold
            << " fill_threshold=" << fill_threshold
            << " issue_period=" << bwc_issue_period
            << " symmetric_mode_epochs=" << symmetric_mode_epoch_count
            << " symmetric_tier1_epochs=" << symmetric_tier1_epoch_count
            << " symmetric_tier2_epochs=" << symmetric_tier2_epoch_count
            << " symmetric_tier3_epochs=" << symmetric_tier3_epoch_count
            << " max_global_avg_mshr_util=" << max_global_avg_mshr_util
            << " max_global_max_mshr_util=" << max_global_max_mshr_util
            << " max_global_avg_llc_rq_util=" << max_global_avg_llc_rq_util
            << " max_global_max_llc_rq_util=" << max_global_max_llc_rq_util
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
  if constexpr (SPP_BWC_ENABLE_SIG_UTIL) {
    std::cout << "[SIG_UTIL] cpu=" << intern_->cpu
              << " enabled_epochs=" << utility_filter_enabled_epochs
              << " filtered_prefetches=" << utility_prefetch_filtered
              << " useful_updates=" << utility_useful_updates
              << " useless_updates=" << utility_useless_updates
              << std::endl;
  }
  if constexpr (SPP_BWC_ENABLE_HEADGATE_CRIT_CONFGATE) {
    std::cout << "[BWC_CRIT] cpu=" << intern_->cpu
              << " conf_epochs=" << headgate_crit_epochs
              << " high_pressure_epochs=" << headgate_crit_high_pressure_epochs
              << " mid_conf_threshold=" << headgate_crit_mid_conf_threshold_current
              << " tail_conf_threshold=" << headgate_crit_tail_conf_threshold_current
              << " tail_fill_conf_threshold=" << headgate_crit_tail_fill_conf_threshold_current
              << " mid_conf_filtered=" << headgate_crit_mid_conf_filtered
              << " tail_conf_filtered=" << headgate_crit_tail_conf_filtered
              << " tail_conf_rescued=" << headgate_crit_tail_conf_rescued
              << " tail_fill_demoted=" << headgate_crit_tail_fill_demoted
              << std::endl;
  }
  if constexpr (SPP_BWC_ENABLE_HEADGATE) {
    std::cout << "[HEADGATE] cpu=" << intern_->cpu
              << " keep_candidates=" << headgate_keep_candidates_current
              << " mid_max_candidates=" << headgate_mid_max_candidates_current
              << " streaming_epochs=" << headgate_streaming_epochs
              << " mid_filtered=" << headgate_mid_filtered
              << " tail_filtered=" << headgate_tail_filtered
              << " tail_demoted=" << headgate_tail_demoted
              << std::endl;
    std::cout << "[BWC_COORD] cpu=" << intern_->cpu
              << " mini_epochs=" << coord_mini_epoch_count
              << " streaming_mini_epochs=" << coord_streaming_mini_epochs
              << " selected_boost_windows=" << coord_selected_boost_windows
              << " phase_owner_windows=" << coord_phase_owner_windows
              << " dynrunway_keep3_windows=" << coord_dynrunway_keep3_windows
              << " dynrunway_midwide_windows=" << coord_dynrunway_midwide_windows
              << " last_selected_boost_core=" << coord_selected_boost_core_current
              << " last_phase_core=" << coord_phase_core_current
              << " last_window_miss_latency_delta=" << coord_last_window_miss_latency_delta
              << std::endl;
    std::cout << "[BWC_BUCKETS] cpu=" << intern_->cpu
              << " issued_head=" << coord_total_bucket_issued.at(SIG_UTIL_BUCKET_HEAD)
              << " issued_mid=" << coord_total_bucket_issued.at(SIG_UTIL_BUCKET_MID)
              << " issued_tail=" << coord_total_bucket_issued.at(SIG_UTIL_BUCKET_TAIL)
              << " fill_head=" << coord_total_bucket_fill.at(SIG_UTIL_BUCKET_HEAD)
              << " fill_mid=" << coord_total_bucket_fill.at(SIG_UTIL_BUCKET_MID)
              << " fill_tail=" << coord_total_bucket_fill.at(SIG_UTIL_BUCKET_TAIL)
              << " useful_head=" << coord_total_bucket_useful.at(SIG_UTIL_BUCKET_HEAD)
              << " useful_mid=" << coord_total_bucket_useful.at(SIG_UTIL_BUCKET_MID)
              << " useful_tail=" << coord_total_bucket_useful.at(SIG_UTIL_BUCKET_TAIL)
              << " useless_head=" << coord_total_bucket_useless.at(SIG_UTIL_BUCKET_HEAD)
              << " useless_mid=" << coord_total_bucket_useless.at(SIG_UTIL_BUCKET_MID)
              << " useless_tail=" << coord_total_bucket_useless.at(SIG_UTIL_BUCKET_TAIL)
              << std::endl;
    std::cout << "[BWC_TIMELY] cpu=" << intern_->cpu
              << " fill_age0=" << coord_total_fill_age_bins.at(0)
              << " fill_age1=" << coord_total_fill_age_bins.at(1)
              << " fill_age2=" << coord_total_fill_age_bins.at(2)
              << " fill_age3=" << coord_total_fill_age_bins.at(3)
              << " useful_age0=" << coord_total_useful_age_bins.at(0)
              << " useful_age1=" << coord_total_useful_age_bins.at(1)
              << " useful_age2=" << coord_total_useful_age_bins.at(2)
              << " useful_age3=" << coord_total_useful_age_bins.at(3)
              << " useless_age0=" << coord_total_useless_age_bins.at(0)
              << " useless_age1=" << coord_total_useless_age_bins.at(1)
              << " useless_age2=" << coord_total_useless_age_bins.at(2)
              << " useless_age3=" << coord_total_useless_age_bins.at(3)
              << std::endl;
  }
}

void spp_bwc::record_pressure_sample()
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

void spp_bwc::finalize_pressure_epoch()
{
  if (pressure_epoch_sample_count == 0)
    return;

  const double avg_epoch_mshr_util = pressure_epoch_mshr_sum / static_cast<double>(pressure_epoch_sample_count);
  const double avg_epoch_llc_rq_util = pressure_epoch_llc_rq_sum / static_cast<double>(pressure_epoch_sample_count);
  pressure_last_epoch_avg_mshr_util = avg_epoch_mshr_util;
  pressure_last_epoch_max_mshr_util = pressure_epoch_mshr_max;
  pressure_last_epoch_avg_llc_rq_util = avg_epoch_llc_rq_util;
  pressure_last_epoch_max_llc_rq_util = pressure_epoch_llc_rq_max;

  ++pressure_epoch_count;
  pressure_epoch_avg_mshr_sum += avg_epoch_mshr_util;
  pressure_epoch_avg_llc_rq_sum += avg_epoch_llc_rq_util;
  pressure_global_mshr_max = std::max(pressure_global_mshr_max, pressure_epoch_mshr_max);
  pressure_global_llc_rq_max = std::max(pressure_global_llc_rq_max, pressure_epoch_llc_rq_max);

  if (avg_epoch_mshr_util >= BWC_THROTTLE_MSHR)
    ++pressure_epoch_avg_mshr_above_thresh;
  if (pressure_epoch_mshr_max >= BWC_THROTTLE_MSHR)
    ++pressure_epoch_max_mshr_above_thresh;
  if (avg_epoch_llc_rq_util >= BWC_THROTTLE_LLC_RQ)
    ++pressure_epoch_avg_llc_rq_above_thresh;
  if (pressure_epoch_llc_rq_max >= BWC_THROTTLE_LLC_RQ)
    ++pressure_epoch_max_llc_rq_above_thresh;

  pressure_epoch_sample_count = 0;
  pressure_epoch_mshr_sum = 0.0;
  pressure_epoch_llc_rq_sum = 0.0;
  pressure_epoch_mshr_max = 0.0;
  pressure_epoch_llc_rq_max = 0.0;
}

bool spp_bwc::bwc_should_issue()
{
  if (bwc_issue_period <= 1) return true;
  return (++bwc_drop_counter % bwc_issue_period) == 0;
}

void spp_bwc::bwc_update_epoch()
{
  const double avg_mshr_util = pressure_last_epoch_avg_mshr_util;
  const double max_mshr_util = pressure_last_epoch_max_mshr_util;
  const double avg_llc_rq_util = pressure_last_epoch_avg_llc_rq_util;
  const double max_llc_rq_util = pressure_last_epoch_max_llc_rq_util;
  const bool epoch_has_prefetch = (fdp_epoch_pf_issued > 0);
  const double accuracy = epoch_has_prefetch
                              ? (static_cast<double>(fdp_epoch_pf_useful) / static_cast<double>(fdp_epoch_pf_issued))
                              : 1.0;
  publish_shared_epoch_state(intern_->cpu, accuracy, avg_mshr_util, max_mshr_util, avg_llc_rq_util, max_llc_rq_util);

  const auto shared = snapshot_shared_epoch_state(BWC_ACC_LOW_THROTTLE);
  const double shared_avg_pressure = std::max(shared.avg_mshr_util, shared.avg_llc_rq_util);
  const double shared_max_pressure = std::max(shared.max_mshr_util, shared.max_llc_rq_util);
  max_global_avg_mshr_util = std::max(max_global_avg_mshr_util, shared.avg_mshr_util);
  max_global_max_mshr_util = std::max(max_global_max_mshr_util, shared.max_mshr_util);
  max_global_avg_llc_rq_util = std::max(max_global_avg_llc_rq_util, shared.avg_llc_rq_util);
  max_global_max_llc_rq_util = std::max(max_global_max_llc_rq_util, shared.max_llc_rq_util);
  current_global_avg_mshr_util = shared.avg_mshr_util;
  current_global_avg_llc_rq_util = shared.avg_llc_rq_util;

  const bool local_pressure_high =
      avg_llc_rq_util > BWC_THROTTLE_LLC_RQ || max_llc_rq_util > BWC_THROTTLE_LLC_RQ ||
      avg_mshr_util > BWC_THROTTLE_MSHR || max_mshr_util > BWC_THROTTLE_MSHR;
  const bool local_pressure_low =
      avg_llc_rq_util < BWC_ACCEL_LLC_RQ && max_llc_rq_util < BWC_THROTTLE_LLC_RQ &&
      avg_mshr_util < BWC_ACCEL_MSHR && max_mshr_util < BWC_THROTTLE_MSHR;
  const bool symmetric_pressure_mode =
      shared.active_cores >= BWC_SYMMETRIC_MIN_CORES &&
      !shared.any_low_accuracy &&
      shared_avg_pressure >= BWC_SYMMETRIC_MSHR_AVG;
  const bool shared_pressure_active =
      shared.active_cores >= 2 &&
      (shared_avg_pressure >= BWC_SYMMETRIC_MSHR_AVG || shared_max_pressure >= BWC_SYMMETRIC_MSHR_AVG);
  const bool streaming_core =
      epoch_has_prefetch && shared_pressure_active && accuracy >= BWC_HEADGATE_STREAMING_ACC;
  const bool four_core_streaming = streaming_core && (shared.active_cores >= 4);

  utility_filter_enabled = false;
  if constexpr (SPP_BWC_ENABLE_HEADGATE) {
    headgate_keep_candidates_current = SPP_BWC_HEADGATE_KEEP_CANDIDATES;
    headgate_mid_max_candidates_current = SPP_BWC_HEADGATE_MID_MAX_CANDIDATES;
    headgate_streaming_keep_tail_active = false;
    headgate_streaming_boost_active = false;
    headgate_crit_mid_conf_threshold_current = SPP_BWC_HEADGATE_CRIT_MID_CONF_THRESHOLD;
    headgate_crit_tail_conf_threshold_current = SPP_BWC_HEADGATE_CRIT_TAIL_CONF_THRESHOLD;
    headgate_crit_tail_fill_conf_threshold_current = SPP_BWC_HEADGATE_CRIT_TAIL_FILL_CONF_THRESHOLD;
    headgate_crit_high_pressure_active = false;
  }
  if constexpr (SPP_BWC_ENABLE_HEADGATE) {
    utility_filter_enabled = streaming_core;
    if (utility_filter_enabled) {
      ++utility_filter_enabled_epochs;
      ++headgate_streaming_epochs;
      if constexpr (SPP_BWC_ENABLE_HEADGATE_CRIT_CONFGATE) {
        ++headgate_crit_epochs;
      }
    }
  } else if constexpr (SPP_BWC_ENABLE_SIG_UTIL) {
    utility_filter_enabled = local_pressure_high || shared_pressure_active;
    if (utility_filter_enabled) {
      ++utility_filter_enabled_epochs;
    }
  }

  if constexpr (SPP_BWC_ENABLE_HEADGATE) {
    if (local_pressure_high) {
      if (fdp_level > 1) {
        --fdp_level;
      }
    } else {
      fdp_level = 3;
    }
  } else {
    bool should_throttle = local_pressure_high;
    if constexpr (SPP_BWC_ENABLE_SIG_UTIL) {
      should_throttle = should_throttle || (epoch_has_prefetch && shared_pressure_active && accuracy < BWC_ACC_LOW_THROTTLE);
    } else {
      should_throttle = should_throttle || (epoch_has_prefetch && accuracy < BWC_ACC_LOW_THROTTLE);
    }

    if (should_throttle) {
      if (fdp_level > 1) --fdp_level;
    } else if (epoch_has_prefetch && local_pressure_low && accuracy > FDP_ACC_HIGH) {
      if (fdp_level < 5) ++fdp_level;
    }
  }

  pf_threshold     = FDP_PF_THRESH[fdp_level];
  fill_threshold   = FDP_FILL_THRESH[fdp_level];
  bwc_issue_period = BWC_ISSUE_PERIOD[fdp_level];
  if constexpr (SPP_BWC_ENABLE_HEADGATE) {
    if (!local_pressure_high) {
      pf_threshold = FDP_PF_THRESH[3];
      fill_threshold = FDP_FILL_THRESH[3];
      bwc_issue_period = 1;
    }
    if constexpr (SPP_BWC_ENABLE_HEADGATE_DYNBOOST || SPP_BWC_ENABLE_HEADGATE_DYNRUNWAY || SPP_BWC_ENABLE_HEADGATE_PHASEROTATE) {
      if (streaming_core) {
        headgate_streaming_keep_tail_active = true;
      }
    } else if constexpr (SPP_BWC_ENABLE_HEADGATE_STAGGERFILL) {
      if (streaming_core && shared.active_cores >= SPP_BWC_HEADGATE_STAGGERFILL_MIN_ACTIVE_CORES) {
        headgate_streaming_keep_tail_active = true;
        pf_threshold = SPP_BWC_HEADGATE_STAGGERFILL_PF_THRESHOLD;
        fill_threshold =
            SPP_BWC_HEADGATE_STAGGERFILL_BASE_FILL_THRESHOLD +
            (SPP_BWC_HEADGATE_STAGGERFILL_FILL_THRESHOLD_STRIDE * static_cast<uint32_t>(intern_->cpu));
        bwc_issue_period = SPP_BWC_HEADGATE_STAGGERFILL_ISSUE_PERIOD;
      }
    } else if constexpr (SPP_BWC_ENABLE_HEADGATE_STAGGERPF) {
      if (streaming_core && shared.active_cores >= SPP_BWC_HEADGATE_STAGGERPF_MIN_ACTIVE_CORES) {
        headgate_streaming_keep_tail_active = true;
        pf_threshold =
            SPP_BWC_HEADGATE_STAGGERPF_BASE_PF_THRESHOLD +
            (SPP_BWC_HEADGATE_STAGGERPF_PF_THRESHOLD_STRIDE * static_cast<uint32_t>(intern_->cpu));
        fill_threshold = SPP_BWC_HEADGATE_STAGGERPF_FILL_THRESHOLD;
        bwc_issue_period = SPP_BWC_HEADGATE_STAGGERPF_ISSUE_PERIOD;
      }
    } else if constexpr (SPP_BWC_ENABLE_HEADGATE_PARITYBOOST) {
      if (four_core_streaming) {
        headgate_streaming_keep_tail_active = true;
        pf_threshold = FDP_PF_THRESH[3];
        fill_threshold = FDP_FILL_THRESH[3];
        bwc_issue_period = 1;
        if ((intern_->cpu % 2) == 0) {
          headgate_streaming_boost_active = true;
        }
      }
    } else {
      if constexpr (SPP_BWC_ENABLE_HEADGATE_STREAMING_KEEP_TAIL) {
        if (streaming_core) {
          headgate_streaming_keep_tail_active = true;
        }
      }
      if constexpr (SPP_BWC_ENABLE_HEADGATE_STREAMING_BOOST) {
        if (streaming_core) {
          headgate_streaming_boost_active = true;
        }
      }
    }
    if (headgate_streaming_boost_active) {
        pf_threshold = SPP_BWC_HEADGATE_STREAMING_BOOST_PF_THRESHOLD;
        fill_threshold = SPP_BWC_HEADGATE_STREAMING_BOOST_FILL_THRESHOLD;
        bwc_issue_period = SPP_BWC_HEADGATE_STREAMING_BOOST_ISSUE_PERIOD;
    }
    if constexpr (SPP_BWC_ENABLE_HEADGATE_CRIT_CONFGATE) {
      if (utility_filter_enabled &&
          (shared_avg_pressure >= BWC_TIER2_MSHR_AVG || shared_max_pressure >= BWC_TIER3_MSHR_AVG)) {
        headgate_crit_high_pressure_active = true;
        ++headgate_crit_high_pressure_epochs;
        headgate_crit_mid_conf_threshold_current =
            std::min<uint32_t>(100, headgate_crit_mid_conf_threshold_current + SPP_BWC_HEADGATE_CRIT_HIGH_PRESSURE_MID_DELTA);
        headgate_crit_tail_conf_threshold_current =
            std::min<uint32_t>(100, headgate_crit_tail_conf_threshold_current + SPP_BWC_HEADGATE_CRIT_HIGH_PRESSURE_TAIL_DELTA);
        headgate_crit_tail_fill_conf_threshold_current =
            std::min<uint32_t>(100, headgate_crit_tail_fill_conf_threshold_current + SPP_BWC_HEADGATE_CRIT_HIGH_PRESSURE_TAIL_DELTA);
      }
    }
  }
  if constexpr (SPP_BWC_ENABLE_GLOBAL_GSP) {
    if (symmetric_pressure_mode) {
      ++symmetric_mode_epoch_count;

      if constexpr (SPP_BWC_ENABLE_TIERED_GLOBAL) {
        if (shared_avg_pressure >= BWC_TIER3_MSHR_AVG) {
          ++symmetric_tier3_epoch_count;
          if constexpr (!SPP_BWC_ENABLE_HEADGATE) {
            bwc_issue_period = std::max(bwc_issue_period, BWC_TIER2_ISSUE_PERIOD);
            pf_threshold = std::max(pf_threshold, BWC_TIER3_PF_THRESHOLD);
          }
        } else if (shared_avg_pressure >= BWC_TIER2_MSHR_AVG) {
          ++symmetric_tier2_epoch_count;
          if constexpr (SPP_BWC_ENABLE_HEADGATE_TIERKEEP) {
            if (utility_filter_enabled) {
              headgate_keep_candidates_current = SPP_BWC_HEADGATE_LIGHT_KEEP_CANDIDATES;
            }
          }
          if constexpr (!SPP_BWC_ENABLE_HEADGATE) {
            bwc_issue_period = std::max(bwc_issue_period, BWC_TIER2_ISSUE_PERIOD);
          }
        } else {
          ++symmetric_tier1_epoch_count;
          if constexpr (SPP_BWC_ENABLE_HEADGATE_TIERKEEP) {
            if (utility_filter_enabled) {
              headgate_keep_candidates_current = SPP_BWC_HEADGATE_LIGHT_KEEP_CANDIDATES;
            }
          }
          if constexpr (!SPP_BWC_ENABLE_HEADGATE) {
            bwc_issue_period = std::max(bwc_issue_period, BWC_SYMMETRIC_ISSUE_PERIOD);
          }
        }
      } else if constexpr (!SPP_BWC_ENABLE_HEADGATE) {
        bwc_issue_period = std::max(bwc_issue_period, BWC_SYMMETRIC_ISSUE_PERIOD);
      }
    }
  }

  coord_streaming_core_active = streaming_core;
  coord_epoch_base_pf_threshold = pf_threshold;
  coord_epoch_base_fill_threshold = fill_threshold;
  coord_epoch_base_issue_period = bwc_issue_period;
  coord_epoch_base_keep_candidates = headgate_keep_candidates_current;
  coord_epoch_base_mid_max_candidates = headgate_mid_max_candidates_current;
  coord_epoch_base_keep_tail = headgate_streaming_keep_tail_active;
  coord_update_runtime_assignment(false);
  coord_apply_runtime_actuator();

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << "[BWC] epoch: issued=" << fdp_epoch_pf_issued
              << " useful=" << fdp_epoch_pf_useful
              << " acc=" << accuracy
              << " avg_mshr_util=" << avg_mshr_util
              << " max_mshr_util=" << max_mshr_util
              << " avg_llc_rq_util=" << avg_llc_rq_util
              << " max_llc_rq_util=" << max_llc_rq_util
              << " shared_avg_mshr=" << shared.avg_mshr_util
              << " shared_max_mshr=" << shared.max_mshr_util
              << " level=" << fdp_level
              << " pf_thresh=" << pf_threshold
              << " symmetric_mode=" << symmetric_pressure_mode
              << " shared_pressure=" << shared_pressure_active
              << " streaming_core=" << streaming_core
              << " utility_filter=" << utility_filter_enabled
              << " issue_period=" << bwc_issue_period << "\n";
  }
}

void spp_bwc::coord_finalize_mini_epoch(bool flush_partial)
{
  if (!flush_partial && coord_accesses_in_mini_epoch == 0) {
    return;
  }

  coord_prev_window_bucket_issued = coord_window_bucket_issued;
  coord_prev_window_bucket_fill = coord_window_bucket_fill;
  coord_prev_window_bucket_useful = coord_window_bucket_useful;
  coord_prev_window_bucket_useless = coord_window_bucket_useless;
  coord_prev_window_fill_age_bins = coord_window_fill_age_bins;
  coord_prev_window_useful_age_bins = coord_window_useful_age_bins;
  coord_prev_window_useless_age_bins = coord_window_useless_age_bins;
  coord_last_window_miss_latency_delta = static_cast<uint64_t>(intern_->sim_stats.total_miss_latency_cycles) - coord_last_miss_latency_cycles;
  coord_last_miss_latency_cycles = static_cast<uint64_t>(intern_->sim_stats.total_miss_latency_cycles);

  const uint64_t timely_useful = coord_prev_window_useful_age_bins.at(0) + coord_prev_window_useful_age_bins.at(1);
  const uint64_t late_useful = coord_prev_window_useful_age_bins.at(COORD_AGE_BIN_COUNT - 1);
  const uint64_t window_useful = coord_prev_window_bucket_useful.at(SIG_UTIL_BUCKET_HEAD) +
                                 coord_prev_window_bucket_useful.at(SIG_UTIL_BUCKET_MID) +
                                 coord_prev_window_bucket_useful.at(SIG_UTIL_BUCKET_TAIL);

  publish_shared_coord_state(intern_->cpu, coord_streaming_core_active, coord_mini_epoch_count, 
                             coord_prev_window_bucket_issued.at(SIG_UTIL_BUCKET_HEAD) +
                                 coord_prev_window_bucket_issued.at(SIG_UTIL_BUCKET_MID) +
                                 coord_prev_window_bucket_issued.at(SIG_UTIL_BUCKET_TAIL),
                             window_useful, coord_prev_window_bucket_useful.at(SIG_UTIL_BUCKET_HEAD), timely_useful, late_useful,
                             coord_last_window_miss_latency_delta);

  if (coord_streaming_core_active) {
    ++coord_streaming_mini_epochs;
  }
  ++coord_mini_epoch_count;
  coord_issue_tag_current = static_cast<uint8_t>(coord_mini_epoch_count & PREFETCH_META_TAG_MASK);

  coord_update_runtime_assignment(true);
  coord_apply_runtime_actuator();

  coord_window_bucket_issued.fill(0);
  coord_window_bucket_fill.fill(0);
  coord_window_bucket_useful.fill(0);
  coord_window_bucket_useless.fill(0);
  coord_window_fill_age_bins.fill(0);
  coord_window_useful_age_bins.fill(0);
  coord_window_useless_age_bins.fill(0);
}

void spp_bwc::coord_update_runtime_assignment(bool count_window_assignment)
{
  if constexpr (!(SPP_BWC_ENABLE_HEADGATE_DYNBOOST || SPP_BWC_ENABLE_HEADGATE_DYNRUNWAY || SPP_BWC_ENABLE_HEADGATE_PHASEROTATE)) {
    return;
  }

  const auto shared_coord = snapshot_shared_coord_state();
  coord_selected_boost_core_current = shared_coord.best_core;
  coord_phase_core_current = shared_coord.phase_core;

  if (!count_window_assignment) {
    return;
  }

  if constexpr (SPP_BWC_ENABLE_HEADGATE_DYNBOOST) {
    if (coord_streaming_core_active && coord_selected_boost_core_current == static_cast<int>(intern_->cpu)) {
      ++coord_selected_boost_windows;
    }
  }
  if constexpr (SPP_BWC_ENABLE_HEADGATE_PHASEROTATE) {
    if (coord_streaming_core_active && coord_phase_core_current == static_cast<int>(intern_->cpu)) {
      ++coord_phase_owner_windows;
    }
  }
}

void spp_bwc::coord_apply_runtime_actuator()
{
  if constexpr (!(SPP_BWC_ENABLE_HEADGATE_DYNBOOST || SPP_BWC_ENABLE_HEADGATE_DYNRUNWAY || SPP_BWC_ENABLE_HEADGATE_PHASEROTATE)) {
    return;
  }

  if constexpr (SPP_BWC_ENABLE_HEADGATE) {
    pf_threshold = coord_epoch_base_pf_threshold;
    fill_threshold = coord_epoch_base_fill_threshold;
    bwc_issue_period = coord_epoch_base_issue_period;
    headgate_keep_candidates_current = coord_epoch_base_keep_candidates;
    headgate_mid_max_candidates_current = coord_epoch_base_mid_max_candidates;
    headgate_streaming_keep_tail_active = coord_epoch_base_keep_tail;
    headgate_streaming_boost_active = false;
  }

  if (!coord_streaming_core_active) {
    return;
  }

  if constexpr (SPP_BWC_ENABLE_HEADGATE_DYNRUNWAY) {
    const uint64_t timely = coord_prev_window_useful_age_bins.at(0) + coord_prev_window_useful_age_bins.at(1);
    const uint64_t late = coord_prev_window_useful_age_bins.at(COORD_AGE_BIN_COUNT - 1) +
                          coord_prev_window_useless_age_bins.at(COORD_AGE_BIN_COUNT - 1);
    const uint64_t mid_useful = coord_prev_window_bucket_useful.at(SIG_UTIL_BUCKET_MID);
    const uint64_t mid_useless = coord_prev_window_bucket_useless.at(SIG_UTIL_BUCKET_MID);
    const uint64_t tail_useful = coord_prev_window_bucket_useful.at(SIG_UTIL_BUCKET_TAIL);
    const uint64_t tail_useless = coord_prev_window_bucket_useless.at(SIG_UTIL_BUCKET_TAIL);

    if ((timely + mid_useful) >= (late + mid_useless) && mid_useful >= mid_useless) {
      headgate_keep_candidates_current =
          std::max<uint32_t>(headgate_keep_candidates_current, SPP_BWC_HEADGATE_DYNRUNWAY_KEEP_CANDIDATES);
      ++coord_dynrunway_keep3_windows;
    }
    if ((timely + tail_useful) >= (late + tail_useless) && tail_useful > 0) {
      headgate_mid_max_candidates_current =
          std::max<uint32_t>(headgate_mid_max_candidates_current, SPP_BWC_HEADGATE_DYNRUNWAY_AGGRESSIVE_MID_MAX_CANDIDATES);
      ++coord_dynrunway_midwide_windows;
    } else if ((late > timely) && (tail_useless > tail_useful)) {
      headgate_mid_max_candidates_current =
          std::min<uint32_t>(headgate_mid_max_candidates_current, SPP_BWC_HEADGATE_DYNRUNWAY_CONSERVATIVE_MID_MAX_CANDIDATES);
    }
  }

  if constexpr (SPP_BWC_ENABLE_HEADGATE_DYNBOOST) {
    if (coord_selected_boost_core_current == static_cast<int>(intern_->cpu)) {
      pf_threshold = std::min<uint32_t>(pf_threshold, SPP_BWC_HEADGATE_DYNBOOST_PF_THRESHOLD);
      fill_threshold = std::min<uint32_t>(fill_threshold, SPP_BWC_HEADGATE_DYNBOOST_FILL_THRESHOLD);
      bwc_issue_period = std::min<uint32_t>(bwc_issue_period, SPP_BWC_HEADGATE_DYNBOOST_ISSUE_PERIOD);
      headgate_keep_candidates_current = std::max<uint32_t>(headgate_keep_candidates_current, SPP_BWC_HEADGATE_DYNBOOST_KEEP_CANDIDATES);
      headgate_mid_max_candidates_current =
          std::max<uint32_t>(headgate_mid_max_candidates_current, SPP_BWC_HEADGATE_DYNBOOST_MID_MAX_CANDIDATES);
      headgate_streaming_keep_tail_active = true;
    }
  }

  if constexpr (SPP_BWC_ENABLE_HEADGATE_PHASEROTATE) {
    if (coord_phase_core_current == static_cast<int>(intern_->cpu)) {
      pf_threshold = std::min<uint32_t>(pf_threshold, SPP_BWC_HEADGATE_PHASEROTATE_PF_THRESHOLD);
      fill_threshold = std::min<uint32_t>(fill_threshold, SPP_BWC_HEADGATE_PHASEROTATE_FILL_THRESHOLD);
      bwc_issue_period = std::min<uint32_t>(bwc_issue_period, SPP_BWC_HEADGATE_PHASEROTATE_ISSUE_PERIOD);
      headgate_keep_candidates_current = std::max<uint32_t>(headgate_keep_candidates_current, SPP_BWC_HEADGATE_PHASEROTATE_KEEP_CANDIDATES);
      headgate_mid_max_candidates_current =
          std::max<uint32_t>(headgate_mid_max_candidates_current, SPP_BWC_HEADGATE_PHASEROTATE_MID_MAX_CANDIDATES);
      headgate_streaming_keep_tail_active = true;
    }
  }
}

void spp_bwc::coord_record_issued(uint8_t rank_bucket)
{
  ++coord_total_bucket_issued.at(rank_bucket);
  ++coord_window_bucket_issued.at(rank_bucket);
}

void spp_bwc::coord_record_fill(uint32_t metadata_in, uint8_t)
{
  const auto meta = decode_prefetch_metadata(metadata_in);
  if (!meta.valid) {
    return;
  }

  const auto age_bin = metadata_age_bin(coord_issue_tag_current, meta.issue_tag);
  ++coord_total_bucket_fill.at(meta.rank_bucket);
  ++coord_window_bucket_fill.at(meta.rank_bucket);
  ++coord_total_fill_age_bins.at(age_bin);
  ++coord_window_fill_age_bins.at(age_bin);
}

void spp_bwc::coord_record_useful(uint32_t metadata_in)
{
  const auto meta = decode_prefetch_metadata(metadata_in);
  if (!meta.valid) {
    return;
  }

  const auto age_bin = metadata_age_bin(coord_issue_tag_current, meta.issue_tag);
  ++coord_total_bucket_useful.at(meta.rank_bucket);
  ++coord_window_bucket_useful.at(meta.rank_bucket);
  ++coord_total_useful_age_bins.at(age_bin);
  ++coord_window_useful_age_bins.at(age_bin);
}

void spp_bwc::coord_record_useless(uint32_t metadata_in)
{
  const auto meta = decode_prefetch_metadata(metadata_in);
  if (!meta.valid) {
    return;
  }

  const auto age_bin = metadata_age_bin(coord_issue_tag_current, meta.issue_tag);
  ++coord_total_bucket_useless.at(meta.rank_bucket);
  ++coord_window_bucket_useless.at(meta.rank_bucket);
  ++coord_total_useless_age_bins.at(age_bin);
  ++coord_window_useless_age_bins.at(age_bin);
}

uint32_t spp_bwc::encode_prefetch_metadata(uint8_t issuer_cpu, uint8_t rank_bucket, uint8_t issue_tag)
{
  return PREFETCH_META_VALID |
         ((static_cast<uint32_t>(rank_bucket) & PREFETCH_META_RANK_MASK) << PREFETCH_META_RANK_SHIFT) |
         ((static_cast<uint32_t>(issuer_cpu) & PREFETCH_META_CPU_MASK) << PREFETCH_META_CPU_SHIFT) |
         ((static_cast<uint32_t>(issue_tag) & PREFETCH_META_TAG_MASK) << PREFETCH_META_TAG_SHIFT);
}

spp_bwc::prefetch_metadata_fields spp_bwc::decode_prefetch_metadata(uint32_t metadata)
{
  prefetch_metadata_fields result{};
  if ((metadata & PREFETCH_META_VALID) == 0) {
    return result;
  }

  result.valid = true;
  result.rank_bucket = static_cast<uint8_t>((metadata >> PREFETCH_META_RANK_SHIFT) & PREFETCH_META_RANK_MASK);
  result.issuer_cpu = static_cast<uint8_t>((metadata >> PREFETCH_META_CPU_SHIFT) & PREFETCH_META_CPU_MASK);
  result.issue_tag = static_cast<uint8_t>((metadata >> PREFETCH_META_TAG_SHIFT) & PREFETCH_META_TAG_MASK);
  return result;
}

uint8_t spp_bwc::metadata_age_bin(uint8_t current_tag, uint8_t issue_tag)
{
  const auto distance = static_cast<uint8_t>(current_tag - issue_tag);
  if (distance <= 1) {
    return 0;
  }
  if (distance <= 3) {
    return 1;
  }
  if (distance <= 7) {
    return 2;
  }
  return 3;
}

uint8_t spp_bwc::utility_rank_bucket(uint32_t candidate_rank) const
{
  if (candidate_rank <= 2) {
    return SIG_UTIL_BUCKET_HEAD;
  }
  if (candidate_rank <= headgate_mid_max_candidates_current) {
    return SIG_UTIL_BUCKET_MID;
  }
  return SIG_UTIL_BUCKET_TAIL;
}

uint32_t spp_bwc::make_utility_key(uint32_t signature, uint8_t rank_bucket)
{
  return (signature << 2) | static_cast<uint32_t>(rank_bucket);
}

bool spp_bwc::utility_allows_prefetch(uint32_t signature, uint8_t rank_bucket) const
{
  if constexpr (!SPP_BWC_ENABLE_SIG_UTIL) {
    return true;
  }

  if (!utility_filter_enabled) {
    return true;
  }

  const auto utility_key = make_utility_key(signature, rank_bucket);
  const auto idx = get_hash(utility_key) % SIG_UTIL_TABLE_SIZE;
  const auto& entry = utility_table.at(idx);
  int min_score = 0;
  if constexpr (SPP_BWC_ENABLE_HEADGATE) {
    if (rank_bucket == SIG_UTIL_BUCKET_MID) {
      min_score = SPP_BWC_HEADGATE_MID_SCORE_THRESHOLD;
    }
  }
  return !entry.valid || entry.tag != utility_key || entry.score >= min_score;
}

int spp_bwc::utility_score_or_default(uint32_t signature, uint8_t rank_bucket) const
{
  if constexpr (!SPP_BWC_ENABLE_SIG_UTIL) {
    return 0;
  }

  const auto utility_key = make_utility_key(signature, rank_bucket);
  const auto idx = get_hash(utility_key) % SIG_UTIL_TABLE_SIZE;
  const auto& entry = utility_table.at(idx);

  if (!entry.valid || entry.tag != utility_key) {
    return 0;
  }

  return entry.score;
}

bool spp_bwc::utility_allows_l2_fill(uint32_t signature, uint8_t rank_bucket) const
{
  if constexpr (!SPP_BWC_ENABLE_SIG_UTIL) {
    return true;
  }

  if (!utility_filter_enabled) {
    return true;
  }

  int min_score = 0;
  if constexpr (SPP_BWC_ENABLE_HEADGATE_L2QUAL) {
    if (rank_bucket == SIG_UTIL_BUCKET_MID) {
      min_score = SPP_BWC_HEADGATE_L2QUAL_MID_SCORE_THRESHOLD;
    } else if (rank_bucket == SIG_UTIL_BUCKET_TAIL) {
      min_score = SPP_BWC_HEADGATE_L2QUAL_TAIL_SCORE_THRESHOLD;
    }
  }

  return utility_score_or_default(signature, rank_bucket) >= min_score;
}

void spp_bwc::utility_record_prefetch(champsim::address addr, uint32_t signature, uint8_t rank_bucket, uint32_t metadata)
{
  if constexpr (!SPP_BWC_ENABLE_SIG_UTIL) {
    return;
  }

  const auto idx = get_hash(champsim::block_number{addr}.to<uint64_t>()) % SIG_UTIL_TABLE_SIZE;
  pending_prefetch_table.at(idx) = pending_prefetch_entry{true, champsim::block_number{addr}, make_utility_key(signature, rank_bucket), metadata};
}

void spp_bwc::utility_record_useful(champsim::address addr)
{
  if constexpr (!SPP_BWC_ENABLE_SIG_UTIL) {
    return;
  }

  const auto line = champsim::block_number{addr};
  const auto idx = get_hash(line.to<uint64_t>()) % SIG_UTIL_TABLE_SIZE;
  auto& entry = pending_prefetch_table.at(idx);
  if (!entry.valid || entry.line != line) {
    return;
  }

  coord_record_useful(entry.metadata);
  utility_update_score(entry.utility_key, SIG_UTIL_USEFUL_DELTA);
  entry.valid = false;
  ++utility_useful_updates;
}

void spp_bwc::utility_record_useless(champsim::address addr)
{
  if constexpr (!SPP_BWC_ENABLE_SIG_UTIL) {
    return;
  }

  const auto line = champsim::block_number{addr};
  const auto idx = get_hash(line.to<uint64_t>()) % SIG_UTIL_TABLE_SIZE;
  auto& entry = pending_prefetch_table.at(idx);
  if (!entry.valid || entry.line != line) {
    return;
  }

  coord_record_useless(entry.metadata);
  utility_update_score(entry.utility_key, SIG_UTIL_USELESS_DELTA);
  entry.valid = false;
  ++utility_useless_updates;
}

void spp_bwc::utility_update_score(uint32_t utility_key, int delta)
{
  if constexpr (!SPP_BWC_ENABLE_SIG_UTIL) {
    return;
  }

  const auto idx = get_hash(utility_key) % SIG_UTIL_TABLE_SIZE;
  auto& entry = utility_table.at(idx);
  if (!entry.valid || entry.tag != utility_key) {
    entry.valid = true;
    entry.tag = utility_key;
    entry.score = 0;
  }

  entry.score = std::clamp(entry.score + delta, SIG_UTIL_SCORE_MIN, SIG_UTIL_SCORE_MAX);
}

// TODO: Find a good 64-bit hash function
uint64_t spp_bwc::get_hash(uint64_t key)
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

void spp_bwc::SIGNATURE_TABLE::read_and_update_sig(champsim::address addr, uint32_t& last_sig, uint32_t& curr_sig, typename offset_type::difference_type& delta)
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

void spp_bwc::PATTERN_TABLE::update_pattern(uint32_t last_sig, typename offset_type::difference_type curr_delta)
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

void spp_bwc::PATTERN_TABLE::read_pattern(uint32_t curr_sig, std::vector<typename offset_type::difference_type>& delta_q, std::vector<uint32_t>& confidence_q,
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

bool spp_bwc::PREFETCH_FILTER::check(champsim::address check_addr, FILTER_REQUEST filter_request)
{
  champsim::block_number cache_line{check_addr};
  auto hash = get_hash(cache_line.to<uint64_t>());
  auto quotient = (hash >> REMAINDER_BIT) & ((1 << QUOTIENT_BIT) - 1);
  auto remainder = hash % (1 << REMAINDER_BIT);

  if constexpr (SPP_DEBUG_PRINT) {
    std::cout << "[FILTER] check_addr: " << check_addr << " hash: " << hash << " quotient: " << quotient << " remainder: " << remainder << std::endl;
  }

  switch (filter_request) {
  case spp_bwc::SPP_L2C_PREFETCH:
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

  case spp_bwc::SPP_LLC_PREFETCH:
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

  case spp_bwc::L2C_DEMAND:
    if ((remainder_tag[quotient] == remainder) && (useful[quotient] == 0)) {
      useful[quotient] = 1;
      _parent->utility_record_useful(check_addr);
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

  case spp_bwc::L2C_EVICT:
    // Decrease global pf_useful counter when there is a useless prefetch (prefetched but not used)
    if (valid[quotient] && !useful[quotient]) {
      _parent->utility_record_useless(check_addr);
      if (_parent->GHR.pf_useful)
        _parent->GHR.pf_useful--;
    }

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

void spp_bwc::GLOBAL_REGISTER::update_entry(uint32_t pf_sig, uint32_t pf_confidence, offset_type pf_offset, typename offset_type::difference_type pf_delta)
{
  // NOTE: GHR implementation is slightly different from the original paper
  // Instead of matching (last_offset + delta), GHR simply stores and matches the pf_offset
  uint32_t min_conf = 100, victim_way = MAX_GHR_ENTRY;

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

    // GHR replacement policy is based on the stored confidence value
    // An entry with the lowest confidence is selected as a victim
    if (confidence[i] < min_conf) {
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

uint32_t spp_bwc::GLOBAL_REGISTER::check_entry(offset_type page_offset)
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
