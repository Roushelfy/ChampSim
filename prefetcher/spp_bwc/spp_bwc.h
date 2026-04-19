#ifndef SPP_BWC_H
#define SPP_BWC_H

#include <array>
#include <cstdint>
#include <vector>

#include "cache.h"
#include "modules.h"
#include "msl/lru_table.h"

#ifndef SPP_BWC_VARIANT_TAG
#define SPP_BWC_VARIANT_TAG "BWC_LOCAL"
#endif

#ifndef SPP_BWC_ENABLE_GLOBAL_GSP
#define SPP_BWC_ENABLE_GLOBAL_GSP 0
#endif

#ifndef SPP_BWC_ENABLE_TIERED_GLOBAL
#define SPP_BWC_ENABLE_TIERED_GLOBAL 0
#endif

struct spp_bwc : public champsim::modules::prefetcher {

  // SPP functional knobs
  constexpr static bool LOOKAHEAD_ON = true;
  constexpr static bool FILTER_ON = true;
  constexpr static bool GHR_ON = true;
  constexpr static bool SPP_SANITY_CHECK = true;
  constexpr static bool SPP_DEBUG_PRINT = false;

  // Signature table parameters
  constexpr static std::size_t ST_SET = 1;
  constexpr static std::size_t ST_WAY = 256;
  constexpr static unsigned ST_TAG_BIT = 16;
  constexpr static unsigned SIG_SHIFT = 3;
  constexpr static unsigned SIG_BIT = 12;
  constexpr static uint32_t SIG_MASK = ((1 << SIG_BIT) - 1);
  constexpr static unsigned SIG_DELTA_BIT = 7;

  // Pattern table parameters
  constexpr static std::size_t PT_SET = 512;
  constexpr static std::size_t PT_WAY = 4;
  constexpr static unsigned C_SIG_BIT = 4;
  constexpr static unsigned C_DELTA_BIT = 4;
  constexpr static uint32_t C_SIG_MAX = ((1 << C_SIG_BIT) - 1);
  constexpr static uint32_t C_DELTA_MAX = ((1 << C_DELTA_BIT) - 1);

  // Prefetch filter parameters
  constexpr static unsigned QUOTIENT_BIT = 10;
  constexpr static unsigned REMAINDER_BIT = 6;
  constexpr static unsigned HASH_BIT = (QUOTIENT_BIT + REMAINDER_BIT + 1);
  constexpr static std::size_t FILTER_SET = (1 << QUOTIENT_BIT);
  // Runtime-adjustable thresholds (BWC controller modifies these per epoch).
  uint32_t fill_threshold = 90;
  uint32_t pf_threshold   = 25;

  // Global register parameters
  constexpr static unsigned GLOBAL_COUNTER_BIT = 10;
  constexpr static uint32_t GLOBAL_COUNTER_MAX = ((1 << GLOBAL_COUNTER_BIT) - 1);
  constexpr static std::size_t MAX_GHR_ENTRY = 8;

  using prefetcher::prefetcher;
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);

  void prefetcher_initialize();
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();

  enum FILTER_REQUEST { SPP_L2C_PREFETCH, SPP_LLC_PREFETCH, L2C_DEMAND, L2C_EVICT }; // Request type for prefetch filter
  static uint64_t get_hash(uint64_t key);

  struct block_in_page_extent : champsim::dynamic_extent {
    block_in_page_extent() : dynamic_extent(champsim::data::bits{LOG2_PAGE_SIZE}, champsim::data::bits{LOG2_BLOCK_SIZE}) {}
  };
  using offset_type = champsim::address_slice<block_in_page_extent>;

  class SIGNATURE_TABLE
  {
    struct tag_extent : champsim::dynamic_extent {
      tag_extent() : dynamic_extent(champsim::data::bits{ST_TAG_BIT + LOG2_PAGE_SIZE}, champsim::data::bits{LOG2_PAGE_SIZE}) {}
    };

  public:
    spp_bwc* _parent;
    using tag_type = champsim::address_slice<tag_extent>;

    bool valid[ST_SET][ST_WAY];
    tag_type tag[ST_SET][ST_WAY];
    offset_type last_offset[ST_SET][ST_WAY];
    uint32_t sig[ST_SET][ST_WAY], lru[ST_SET][ST_WAY];

    SIGNATURE_TABLE()
    {
      for (uint32_t set = 0; set < ST_SET; set++)
        for (uint32_t way = 0; way < ST_WAY; way++) {
          valid[set][way] = 0;
          tag[set][way] = tag_type{};
          last_offset[set][way] = offset_type{};
          sig[set][way] = 0;
          lru[set][way] = way;
        }
    };

    void read_and_update_sig(champsim::address addr, uint32_t& last_sig, uint32_t& curr_sig, typename offset_type::difference_type& delta);
  };

  class PATTERN_TABLE
  {
  public:
    spp_bwc* _parent;
    typename offset_type::difference_type delta[PT_SET][PT_WAY];
    uint32_t c_delta[PT_SET][PT_WAY], c_sig[PT_SET];

    PATTERN_TABLE()
    {
      for (uint32_t set = 0; set < PT_SET; set++) {
        for (uint32_t way = 0; way < PT_WAY; way++) {
          delta[set][way] = 0;
          c_delta[set][way] = 0;
        }
        c_sig[set] = 0;
      }
    }

    void update_pattern(uint32_t last_sig, typename offset_type::difference_type curr_delta);
    void read_pattern(uint32_t curr_sig, std::vector<typename offset_type::difference_type>& prefetch_delta, std::vector<uint32_t>& confidence_q,
                      uint32_t& lookahead_way, uint32_t& lookahead_conf, uint32_t& pf_q_tail, uint32_t& depth,
                      uint32_t pf_thresh); // pf_thresh passed by value to avoid _parent ambiguity
  };

  class PREFETCH_FILTER
  {
  public:
    spp_bwc* _parent;
    uint64_t remainder_tag[FILTER_SET];
    bool valid[FILTER_SET], // Consider this as "prefetched"
        useful[FILTER_SET]; // Consider this as "used"

    PREFETCH_FILTER()
    {
      for (uint32_t set = 0; set < FILTER_SET; set++) {
        remainder_tag[set] = 0;
        valid[set] = 0;
        useful[set] = 0;
      }
    }

    bool check(champsim::address pf_addr, FILTER_REQUEST filter_request);
  };

  class GLOBAL_REGISTER
  {
  public:
    spp_bwc* _parent;
    // Global counters to calculate global prefetching accuracy
    uint32_t pf_useful, pf_issued;
    uint32_t global_accuracy; // Alpha value in Section III. Equation 3

    // Global History Register (GHR) entries
    uint8_t valid[MAX_GHR_ENTRY];
    uint32_t sig[MAX_GHR_ENTRY], confidence[MAX_GHR_ENTRY];
    offset_type offset[MAX_GHR_ENTRY];
    typename offset_type::difference_type delta[MAX_GHR_ENTRY];

    GLOBAL_REGISTER()
    {
      pf_useful = 0;
      pf_issued = 0;
      global_accuracy = 0;

      for (uint32_t i = 0; i < MAX_GHR_ENTRY; i++) {
        valid[i] = 0;
        sig[i] = 0;
        confidence[i] = 0;
        offset[i] = offset_type{};
        delta[i] = 0;
      }
    }

    void update_entry(uint32_t pf_sig, uint32_t pf_confidence, offset_type pf_offset, typename offset_type::difference_type pf_delta);
    uint32_t check_entry(offset_type page_offset);
  };

  SIGNATURE_TABLE ST;
  PATTERN_TABLE PT;
  PREFETCH_FILTER FILTER;
  GLOBAL_REGISTER GHR;

  // ── BWC (Bandwidth-Aware Controller) ───────────────────────────────────────
  // No toggle flag: spp_bwc is always the BWC version.
  // Baseline comparisons use spp_orig (pure SPP) and spp_dev (FDP).
  static constexpr uint64_t FDP_EPOCH_SIZE = 500;
  static constexpr double   FDP_ACC_HIGH   = 0.80;
  static constexpr double   FDP_ACC_LOW    = 0.50;

  // Queue-pressure sensor thresholds
  static constexpr double BWC_THROTTLE_LLC_RQ = 0.80;
  static constexpr double BWC_THROTTLE_MSHR   = 0.85;
  static constexpr double BWC_ACCEL_LLC_RQ    = 0.30;
  static constexpr double BWC_ACCEL_MSHR      = 0.50;
  static constexpr bool   BWC_ENABLE_SYMMETRIC_SATURATION = false;
  static constexpr double BWC_SYM_THROTTLE_LLC_RQ = 0.60;
  static constexpr double BWC_SYM_THROTTLE_MSHR   = 0.60;
  static constexpr double BWC_SYM_ACCEL_LLC_RQ    = 0.35;
  static constexpr double BWC_SYM_ACCEL_MSHR      = 0.35;
  static constexpr double BWC_SYM_ACCEL_ACC       = 0.05;
  static constexpr uint32_t BWC_CONGESTED_EPOCHS  = 2;
  static constexpr uint32_t BWC_RELAXED_EPOCHS    = 3;
  static constexpr double BWC_SYMMETRIC_MSHR_AVG  = 0.30;
  static constexpr double BWC_TIER2_MSHR_AVG      = 0.38;
  static constexpr double BWC_TIER3_MSHR_AVG      = 0.44;
  static constexpr uint32_t BWC_SYMMETRIC_ISSUE_PERIOD = 2;
  static constexpr uint32_t BWC_TIER2_ISSUE_PERIOD     = 4;
  static constexpr uint32_t BWC_TIER3_PF_THRESHOLD     = 60;
  static constexpr uint32_t BWC_SYMMETRIC_MIN_CORES    = 4;
  // Accuracy-based fallback: throttle if accuracy is near-zero (catches pointer-chasing)
  static constexpr double BWC_ACC_LOW_THROTTLE = 0.01; // < 1% useful → throttle regardless of queue pressure
  uint64_t fdp_access_count    = 0;
  uint64_t fdp_epoch_pf_issued = 0;
  uint64_t fdp_epoch_pf_useful = 0;
  int      fdp_level           = 3;
  uint32_t bwc_congested_epochs = 0;
  uint32_t bwc_relaxed_epochs   = 0;

  // Measurement-only pressure tracking. This does not change controller behavior.
  uint64_t pressure_epoch_sample_count = 0;
  double   pressure_epoch_mshr_sum     = 0.0;
  double   pressure_epoch_llc_rq_sum   = 0.0;
  double   pressure_epoch_mshr_max     = 0.0;
  double   pressure_epoch_llc_rq_max   = 0.0;

  uint64_t pressure_epoch_count                  = 0;
  double   pressure_epoch_avg_mshr_sum           = 0.0;
  double   pressure_epoch_avg_llc_rq_sum         = 0.0;
  double   pressure_global_mshr_max              = 0.0;
  double   pressure_global_llc_rq_max            = 0.0;
  uint64_t pressure_epoch_avg_mshr_above_thresh  = 0;
  uint64_t pressure_epoch_max_mshr_above_thresh  = 0;
  uint64_t pressure_epoch_avg_llc_rq_above_thresh = 0;
  uint64_t pressure_epoch_max_llc_rq_above_thresh = 0;

  // Lookup tables indexed by fdp_level (1-based; index 0 is unused padding)
  static constexpr uint32_t FDP_PF_THRESH[6]    = {0, 80, 60, 25, 15,  5};
  static constexpr uint32_t FDP_FILL_THRESH[6]  = {0, 90, 90, 90, 75, 50};
  // Rate-limiter period: issue every N-th candidate (1 = no limit, 2 = 50%, 4 = 25%)
  static constexpr uint32_t BWC_ISSUE_PERIOD[6] = {0,  4,  2,  1,  1,  1};

  uint32_t bwc_issue_period = 1;
  uint32_t bwc_drop_counter = 0;

  // Global symmetric-pressure mode bookkeeping.
  uint64_t symmetric_mode_epoch_count = 0;
  double   max_global_avg_mshr_util   = 0.0;
  double   max_global_max_mshr_util   = 0.0;
  double   max_global_avg_llc_rq_util = 0.0;
  double   max_global_max_llc_rq_util = 0.0;
  double   current_global_avg_mshr_util = 0.0;
  double   current_global_avg_llc_rq_util = 0.0;
  uint64_t symmetric_tier1_epoch_count = 0;
  uint64_t symmetric_tier2_epoch_count = 0;
  uint64_t symmetric_tier3_epoch_count = 0;

  void record_pressure_sample();
  void finalize_pressure_epoch();
  [[nodiscard]] bool bwc_should_issue();
  void bwc_update_epoch();
  // ────────────────────────────────────────────────────────────────────────────
};

#endif
