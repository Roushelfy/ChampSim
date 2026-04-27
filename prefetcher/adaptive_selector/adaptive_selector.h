#ifndef ADAPTIVE_SELECTOR_H
#define ADAPTIVE_SELECTOR_H

#include <cstddef>
#include <cstdint>
#include <deque>

#include "access_type.h"
#include "cache.h"
#include "modules.h"
#include "../bop/bop.h"
#include "../spp_dev/spp_dev.h"
#include "../spp_orig/spp_orig.h"

struct adaptive_selector : public champsim::modules::prefetcher {
  explicit adaptive_selector(CACHE* cache);

  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr,
                                 uint32_t metadata_in);
  void prefetcher_initialize();
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();

private:
  enum class expert_mode : uint8_t { none = 0, orig = 1, fdp = 2, bop = 3 };

  struct window_entry {
    uint64_t line = 0;
    uint64_t page = 0;
    bool is_rfo = false;
  };

  struct window_features {
    std::size_t observations = 0;
    double rfo_share = 0.0;
    double page_growth = 0.0;
    double line_growth = 0.0;
    double small_delta_ratio = 0.0;
  };

  struct shared_snapshot {
    double local_pressure = 0.0;
    double global_max_pressure = 0.0;
    double global_avg_pressure = 0.0;
    double peer_pressure = 0.0;
    double peer_page_growth = 0.0;
    double peer_line_growth = 0.0;
    double peer_small_delta_ratio = 0.0;
    uint32_t bop_cores = 0;
    uint32_t low_pressure_streak = 0;
    uint32_t peer_ready_cores = 0;
    bool pressure_active = false;
    bool pair_scope_active = false;
    bool peer_lbm_like = false;
    bool peer_lbm_pair_like = false;
    bool peer_high_page_growth = false;
  };

  spp_orig orig_expert;
  spp_dev fdp_expert;
  bop bop_expert;

  bool orig_initialized = false;
  bool fdp_initialized = false;
  bool bop_initialized = false;

  std::deque<window_entry> history{};

  expert_mode active_mode = expert_mode::none;
  expert_mode last_candidate = expert_mode::none;
  window_features last_features{};

  uint64_t demand_observations = 0;
  uint64_t evaluation_count = 0;
  uint64_t switch_count = 0;
  uint64_t pressure_block_count = 0;
  uint64_t pressure_remap_count = 0;
  uint64_t pressure_sample_count = 0;
  uint32_t candidate_streak = 0;
  bool locked = false;
  uint32_t fdp_lock_delay_remaining = 0;
  uint32_t cpu_index = 0;

  std::size_t window_size = 64;
  std::size_t eval_stride = 16;
  uint32_t decision_streak = 2;
  bool lock_after_switch = true;
  double orig_page_growth_max = 0.40;
  double fdp_page_growth_max = 0.17;
  double orig_small_delta_min = 0.70;
  std::size_t bop_min_observations = 0;
  bool disable_bop = false;
  bool shared_coord_enable = false;
  bool shared_refresh_on_eval_only = true;
  bool shared_pressure_use_avg = false;
  bool shared_block_bop_switch = true;
  bool shared_demote_bop = true;
  bool shared_promote_fdp = false;
  bool shared_peer_lbm_protect = false;
  bool shared_force_bop_demote = false;
  bool shared_pair_disable_bop = true;
  bool shared_pair_hold_for_peer = true;
  bool shared_pair_promote_fdp_on_peer_high_page = true;
  double shared_pressure_on = 0.52;
  double shared_pressure_off = 0.38;
  uint32_t shared_pressure_streak = 16;
  uint32_t shared_bop_grant_low_streak = 0;
  double shared_pressure_mshr_weight = 0.60;
  double shared_pressure_pq_weight = 0.35;
  double shared_pressure_rq_weight = 0.15;
  double shared_peer_lbm_rfo_min = 0.0;
  double shared_peer_lbm_page_max = 0.0;
  double shared_peer_lbm_small_delta_min = 0.0;
  bool shared_pair_peer_lbm_enable = false;
  uint32_t shared_pair_cpu_count = 2;
  double shared_pair_peer_lbm_page_max = 0.17;
  double shared_pair_peer_lbm_small_delta_min = 0.70;
  double shared_peer_high_page_min = 0.40;
  bool shared_pair_promote_fdp = false;
  double shared_pair_orig_to_fdp_page_min = 0.17;
  double shared_pair_orig_to_fdp_small_delta_max = 0.70;
  bool shared_pair_two_low_page_split = true;
  double shared_pair_low_page_max = 0.18;
  double shared_pair_dense_delta_gap = 0.18;
  bool shared_pair_bop_probation_enable = true;
  double shared_pair_bop_page_min = 0.45;
  double shared_pair_bop_page_max = 0.85;
  double shared_pair_bop_peer_page_max = 0.17;
  double shared_pair_bop_peer_small_delta_min = 0.70;
  double shared_pair_bop_pressure_max = 0.42;
  bool shared_pair_high_page_symmetric_enable = true;
  double shared_pair_high_page_bop_min = 0.45;
  double shared_pair_high_page_bop_max = 0.85;
  double shared_pair_high_page_fdp_min = 0.90;
  double shared_pair_high_page_sparse_delta_max = 0.25;
  bool shared_pair_delay_low_page_fdp_lock = true;
  uint32_t shared_pair_fdp_lock_delay_evals = 3;
  double shared_pair_delay_fdp_page_max = 0.18;
  double shared_pair_delay_fdp_peer_page_max = 0.20;
  double shared_pair_delay_fdp_small_delta_min = 0.50;
  bool shared_pair_low_page_fdp_probe_delay_enable = true;
  double shared_pair_low_page_probe_page_max = 0.20;
  double shared_pair_low_page_probe_small_delta_max = 0.70;
  uint32_t shared_pair_low_page_probe_streak = 3;
  double shared_orig_to_fdp_page_max = 0.0;
  double shared_orig_to_fdp_small_delta_min = 0.0;
  double shared_orig_to_fdp_small_delta_max = 1.0;
  bool shared_orig_to_fdp_on_peer_high_page = false;
  shared_snapshot last_shared{};

  void configure_from_env();
  void ensure_initialized(expert_mode mode);
  void observe_demand(champsim::address addr, access_type type);
  bool ready_to_evaluate() const;
  window_features compute_window_features() const;
  expert_mode classify_window(const window_features& features) const;
  void evaluate_and_update();
  double compute_local_pressure() const;
  void refresh_shared_pressure();
  expert_mode coordinate_candidate(expert_mode candidate, const window_features& features);
  bool should_delay_low_page_fdp_lock(const window_features& features) const;

  uint32_t forward_cache_operate(expert_mode mode, champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                 uint32_t metadata_in);
  uint32_t forward_cache_fill(expert_mode mode, champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr,
                              uint32_t metadata_in);
  void forward_cycle_operate(expert_mode mode);

  static const char* mode_name(expert_mode mode);
  static uint32_t source_tag(uint32_t metadata);
  static uint32_t strip_source_tag(uint32_t metadata);
};

#endif
