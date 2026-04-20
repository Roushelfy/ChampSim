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
  uint32_t candidate_streak = 0;
  bool locked = false;

  std::size_t window_size = 64;
  std::size_t eval_stride = 16;
  uint32_t decision_streak = 2;
  bool lock_after_switch = true;
  double orig_page_growth_max = 0.40;
  double fdp_page_growth_max = 0.17;

  void configure_from_env();
  void ensure_initialized(expert_mode mode);
  void observe_demand(champsim::address addr, access_type type);
  bool ready_to_evaluate() const;
  window_features compute_window_features() const;
  expert_mode classify_window(const window_features& features) const;
  void evaluate_and_update();

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
