#ifndef SPP_BWC_H
#define SPP_BWC_H

#include <array>
#include <cstdint>
#include <vector>

#include "cache.h"
#include "modules.h"
#include "msl/lru_table.h"

#ifndef SPP_BWC_VARIANT_TAG
#define SPP_BWC_VARIANT_TAG "BWC"
#endif

#ifndef SPP_BWC_ENABLE_GLOBAL_GSP
#define SPP_BWC_ENABLE_GLOBAL_GSP 0
#endif

#ifndef SPP_BWC_ENABLE_TIERED_GLOBAL
#define SPP_BWC_ENABLE_TIERED_GLOBAL 0
#endif

#ifndef SPP_BWC_ENABLE_SIG_UTIL
#define SPP_BWC_ENABLE_SIG_UTIL 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE
#define SPP_BWC_ENABLE_HEADGATE 0
#endif

#ifndef SPP_BWC_HEADGATE_KEEP_CANDIDATES
#define SPP_BWC_HEADGATE_KEEP_CANDIDATES 2
#endif

#ifndef SPP_BWC_HEADGATE_MID_MAX_CANDIDATES
#define SPP_BWC_HEADGATE_MID_MAX_CANDIDATES 4
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_TIERKEEP
#define SPP_BWC_ENABLE_HEADGATE_TIERKEEP 0
#endif

#ifndef SPP_BWC_HEADGATE_LIGHT_KEEP_CANDIDATES
#define SPP_BWC_HEADGATE_LIGHT_KEEP_CANDIDATES 3
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_TAIL_UTILITY
#define SPP_BWC_ENABLE_HEADGATE_TAIL_UTILITY 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_TAIL_DEMOTE
#define SPP_BWC_ENABLE_HEADGATE_TAIL_DEMOTE 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_L2QUAL
#define SPP_BWC_ENABLE_HEADGATE_L2QUAL 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_STREAMING_BOOST
#define SPP_BWC_ENABLE_HEADGATE_STREAMING_BOOST 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_STREAMING_KEEP_TAIL
#define SPP_BWC_ENABLE_HEADGATE_STREAMING_KEEP_TAIL 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_PARITYBOOST
#define SPP_BWC_ENABLE_HEADGATE_PARITYBOOST 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_STAGGERPF
#define SPP_BWC_ENABLE_HEADGATE_STAGGERPF 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_STAGGERFILL
#define SPP_BWC_ENABLE_HEADGATE_STAGGERFILL 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_DYNBOOST
#define SPP_BWC_ENABLE_HEADGATE_DYNBOOST 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_DYNRUNWAY
#define SPP_BWC_ENABLE_HEADGATE_DYNRUNWAY 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_PHASEROTATE
#define SPP_BWC_ENABLE_HEADGATE_PHASEROTATE 0
#endif

#ifndef SPP_BWC_ENABLE_HEADGATE_CRIT_CONFGATE
#define SPP_BWC_ENABLE_HEADGATE_CRIT_CONFGATE 0
#endif

#ifndef SPP_BWC_COORD_MINI_EPOCH_ACCESSES
#define SPP_BWC_COORD_MINI_EPOCH_ACCESSES 125
#endif

#ifndef SPP_BWC_HEADGATE_STREAMING_BOOST_PF_THRESHOLD
#define SPP_BWC_HEADGATE_STREAMING_BOOST_PF_THRESHOLD 15
#endif

#ifndef SPP_BWC_HEADGATE_STREAMING_BOOST_FILL_THRESHOLD
#define SPP_BWC_HEADGATE_STREAMING_BOOST_FILL_THRESHOLD 75
#endif

#ifndef SPP_BWC_HEADGATE_STREAMING_BOOST_ISSUE_PERIOD
#define SPP_BWC_HEADGATE_STREAMING_BOOST_ISSUE_PERIOD 1
#endif

#ifndef SPP_BWC_HEADGATE_STAGGERPF_MIN_ACTIVE_CORES
#define SPP_BWC_HEADGATE_STAGGERPF_MIN_ACTIVE_CORES 4
#endif

#ifndef SPP_BWC_HEADGATE_STAGGERPF_BASE_PF_THRESHOLD
#define SPP_BWC_HEADGATE_STAGGERPF_BASE_PF_THRESHOLD 10
#endif

#ifndef SPP_BWC_HEADGATE_STAGGERPF_PF_THRESHOLD_STRIDE
#define SPP_BWC_HEADGATE_STAGGERPF_PF_THRESHOLD_STRIDE 5
#endif

#ifndef SPP_BWC_HEADGATE_STAGGERPF_FILL_THRESHOLD
#define SPP_BWC_HEADGATE_STAGGERPF_FILL_THRESHOLD 75
#endif

#ifndef SPP_BWC_HEADGATE_STAGGERPF_ISSUE_PERIOD
#define SPP_BWC_HEADGATE_STAGGERPF_ISSUE_PERIOD 1
#endif

#ifndef SPP_BWC_HEADGATE_STAGGERFILL_MIN_ACTIVE_CORES
#define SPP_BWC_HEADGATE_STAGGERFILL_MIN_ACTIVE_CORES 4
#endif

#ifndef SPP_BWC_HEADGATE_STAGGERFILL_PF_THRESHOLD
#define SPP_BWC_HEADGATE_STAGGERFILL_PF_THRESHOLD 25
#endif

#ifndef SPP_BWC_HEADGATE_STAGGERFILL_BASE_FILL_THRESHOLD
#define SPP_BWC_HEADGATE_STAGGERFILL_BASE_FILL_THRESHOLD 70
#endif

#ifndef SPP_BWC_HEADGATE_STAGGERFILL_FILL_THRESHOLD_STRIDE
#define SPP_BWC_HEADGATE_STAGGERFILL_FILL_THRESHOLD_STRIDE 5
#endif

#ifndef SPP_BWC_HEADGATE_STAGGERFILL_ISSUE_PERIOD
#define SPP_BWC_HEADGATE_STAGGERFILL_ISSUE_PERIOD 1
#endif

#ifndef SPP_BWC_HEADGATE_MID_SCORE_THRESHOLD
#define SPP_BWC_HEADGATE_MID_SCORE_THRESHOLD 0
#endif

#ifndef SPP_BWC_HEADGATE_L2QUAL_MID_SCORE_THRESHOLD
#define SPP_BWC_HEADGATE_L2QUAL_MID_SCORE_THRESHOLD 0
#endif

#ifndef SPP_BWC_HEADGATE_L2QUAL_TAIL_SCORE_THRESHOLD
#define SPP_BWC_HEADGATE_L2QUAL_TAIL_SCORE_THRESHOLD 4
#endif

#ifndef SPP_BWC_HEADGATE_DYNBOOST_PF_THRESHOLD
#define SPP_BWC_HEADGATE_DYNBOOST_PF_THRESHOLD 10
#endif

#ifndef SPP_BWC_HEADGATE_DYNBOOST_FILL_THRESHOLD
#define SPP_BWC_HEADGATE_DYNBOOST_FILL_THRESHOLD 75
#endif

#ifndef SPP_BWC_HEADGATE_DYNBOOST_ISSUE_PERIOD
#define SPP_BWC_HEADGATE_DYNBOOST_ISSUE_PERIOD 1
#endif

#ifndef SPP_BWC_HEADGATE_DYNBOOST_KEEP_CANDIDATES
#define SPP_BWC_HEADGATE_DYNBOOST_KEEP_CANDIDATES 3
#endif

#ifndef SPP_BWC_HEADGATE_DYNBOOST_MID_MAX_CANDIDATES
#define SPP_BWC_HEADGATE_DYNBOOST_MID_MAX_CANDIDATES 6
#endif

#ifndef SPP_BWC_HEADGATE_DYNRUNWAY_KEEP_CANDIDATES
#define SPP_BWC_HEADGATE_DYNRUNWAY_KEEP_CANDIDATES 3
#endif

#ifndef SPP_BWC_HEADGATE_DYNRUNWAY_AGGRESSIVE_MID_MAX_CANDIDATES
#define SPP_BWC_HEADGATE_DYNRUNWAY_AGGRESSIVE_MID_MAX_CANDIDATES 6
#endif

#ifndef SPP_BWC_HEADGATE_DYNRUNWAY_CONSERVATIVE_MID_MAX_CANDIDATES
#define SPP_BWC_HEADGATE_DYNRUNWAY_CONSERVATIVE_MID_MAX_CANDIDATES 3
#endif

#ifndef SPP_BWC_HEADGATE_PHASEROTATE_PF_THRESHOLD
#define SPP_BWC_HEADGATE_PHASEROTATE_PF_THRESHOLD 12
#endif

#ifndef SPP_BWC_HEADGATE_PHASEROTATE_FILL_THRESHOLD
#define SPP_BWC_HEADGATE_PHASEROTATE_FILL_THRESHOLD 75
#endif

#ifndef SPP_BWC_HEADGATE_PHASEROTATE_ISSUE_PERIOD
#define SPP_BWC_HEADGATE_PHASEROTATE_ISSUE_PERIOD 1
#endif

#ifndef SPP_BWC_HEADGATE_PHASEROTATE_KEEP_CANDIDATES
#define SPP_BWC_HEADGATE_PHASEROTATE_KEEP_CANDIDATES 3
#endif

#ifndef SPP_BWC_HEADGATE_PHASEROTATE_MID_MAX_CANDIDATES
#define SPP_BWC_HEADGATE_PHASEROTATE_MID_MAX_CANDIDATES 5
#endif

#ifndef SPP_BWC_HEADGATE_CRIT_MID_CONF_THRESHOLD
#define SPP_BWC_HEADGATE_CRIT_MID_CONF_THRESHOLD 35
#endif

#ifndef SPP_BWC_HEADGATE_CRIT_TAIL_CONF_THRESHOLD
#define SPP_BWC_HEADGATE_CRIT_TAIL_CONF_THRESHOLD 60
#endif

#ifndef SPP_BWC_HEADGATE_CRIT_TAIL_FILL_CONF_THRESHOLD
#define SPP_BWC_HEADGATE_CRIT_TAIL_FILL_CONF_THRESHOLD 90
#endif

#ifndef SPP_BWC_HEADGATE_CRIT_HIGH_PRESSURE_MID_DELTA
#define SPP_BWC_HEADGATE_CRIT_HIGH_PRESSURE_MID_DELTA 5
#endif

#ifndef SPP_BWC_HEADGATE_CRIT_HIGH_PRESSURE_TAIL_DELTA
#define SPP_BWC_HEADGATE_CRIT_HIGH_PRESSURE_TAIL_DELTA 5
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
  static constexpr double BWC_SYMMETRIC_MSHR_AVG = 0.30;
  static constexpr double BWC_TIER2_MSHR_AVG = 0.38;
  static constexpr double BWC_TIER3_MSHR_AVG = 0.44;
  static constexpr uint32_t BWC_SYMMETRIC_ISSUE_PERIOD = 2;
  static constexpr uint32_t BWC_TIER2_ISSUE_PERIOD = 4;
  static constexpr uint32_t BWC_TIER3_PF_THRESHOLD = 60;
  static constexpr uint32_t BWC_SYMMETRIC_MIN_CORES = 4;
  // Accuracy-based fallback: throttle if accuracy is near-zero (catches pointer-chasing)
  static constexpr double BWC_ACC_LOW_THROTTLE = 0.01; // < 1% useful → throttle regardless of queue pressure
  static constexpr double BWC_HEADGATE_STREAMING_ACC = 0.02;
  uint64_t fdp_access_count    = 0;
  uint64_t fdp_epoch_pf_issued = 0;
  uint64_t fdp_epoch_pf_useful = 0;
  int      fdp_level           = 3;

  // Measurement-only pressure tracking. This does not change controller behavior.
  uint64_t pressure_epoch_sample_count = 0;
  double   pressure_epoch_mshr_sum     = 0.0;
  double   pressure_epoch_llc_rq_sum   = 0.0;
  double   pressure_epoch_mshr_max     = 0.0;
  double   pressure_epoch_llc_rq_max   = 0.0;
  double   pressure_last_epoch_avg_mshr_util = 0.0;
  double   pressure_last_epoch_max_mshr_util = 0.0;
  double   pressure_last_epoch_avg_llc_rq_util = 0.0;
  double   pressure_last_epoch_max_llc_rq_util = 0.0;

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

  // Signature utility filter bookkeeping for pressure-aware gating.
  static constexpr std::size_t SIG_UTIL_TABLE_SIZE = 512;
  static constexpr int SIG_UTIL_SCORE_MIN = -16;
  static constexpr int SIG_UTIL_SCORE_MAX = 31;
  static constexpr int SIG_UTIL_USEFUL_DELTA = 4;
  static constexpr int SIG_UTIL_USELESS_DELTA = -1;
  static constexpr uint8_t SIG_UTIL_BUCKET_HEAD = 0;
  static constexpr uint8_t SIG_UTIL_BUCKET_MID = 1;
  static constexpr uint8_t SIG_UTIL_BUCKET_TAIL = 2;
  static constexpr std::size_t COORD_AGE_BIN_COUNT = 4;
  static constexpr uint32_t PREFETCH_META_VALID = (1u << 31);
  static constexpr uint32_t PREFETCH_META_RANK_MASK = 0x3u;
  static constexpr uint32_t PREFETCH_META_CPU_MASK = 0xFu;
  static constexpr uint32_t PREFETCH_META_TAG_MASK = 0xFFu;
  static constexpr uint32_t PREFETCH_META_RANK_SHIFT = 0;
  static constexpr uint32_t PREFETCH_META_CPU_SHIFT = 2;
  static constexpr uint32_t PREFETCH_META_TAG_SHIFT = 6;

  struct signature_utility_entry {
    bool valid = false;
    uint32_t tag = 0;
    int score = 0;
  };

  struct prefetch_metadata_fields {
    bool valid = false;
    uint8_t rank_bucket = 0;
    uint8_t issuer_cpu = 0;
    uint8_t issue_tag = 0;
  };

  struct pending_prefetch_entry {
    bool valid = false;
    champsim::block_number line{};
    uint32_t utility_key = 0;
    uint32_t metadata = 0;
  };

  std::array<signature_utility_entry, SIG_UTIL_TABLE_SIZE> utility_table{};
  std::array<pending_prefetch_entry, SIG_UTIL_TABLE_SIZE> pending_prefetch_table{};
  bool utility_filter_enabled = false;
  uint64_t utility_filter_enabled_epochs = 0;
  uint64_t utility_prefetch_filtered = 0;
  uint64_t utility_useful_updates = 0;
  uint64_t utility_useless_updates = 0;
  uint64_t headgate_streaming_epochs = 0;
  uint64_t headgate_mid_filtered = 0;
  uint64_t headgate_tail_filtered = 0;
  uint64_t headgate_tail_demoted = 0;
  uint32_t headgate_keep_candidates_current = SPP_BWC_HEADGATE_KEEP_CANDIDATES;
  uint32_t headgate_mid_max_candidates_current = SPP_BWC_HEADGATE_MID_MAX_CANDIDATES;
  bool headgate_streaming_keep_tail_active = false;
  bool headgate_streaming_boost_active = false;
  uint32_t headgate_crit_mid_conf_threshold_current = 0;
  uint32_t headgate_crit_tail_conf_threshold_current = 0;
  uint32_t headgate_crit_tail_fill_conf_threshold_current = 100;
  bool headgate_crit_high_pressure_active = false;
  uint64_t headgate_crit_epochs = 0;
  uint64_t headgate_crit_high_pressure_epochs = 0;
  uint64_t headgate_crit_mid_conf_filtered = 0;
  uint64_t headgate_crit_tail_conf_filtered = 0;
  uint64_t headgate_crit_tail_conf_rescued = 0;
  uint64_t headgate_crit_tail_fill_demoted = 0;

  std::array<uint64_t, 3> coord_total_bucket_issued{};
  std::array<uint64_t, 3> coord_total_bucket_fill{};
  std::array<uint64_t, 3> coord_total_bucket_useful{};
  std::array<uint64_t, 3> coord_total_bucket_useless{};
  std::array<uint64_t, COORD_AGE_BIN_COUNT> coord_total_fill_age_bins{};
  std::array<uint64_t, COORD_AGE_BIN_COUNT> coord_total_useful_age_bins{};
  std::array<uint64_t, COORD_AGE_BIN_COUNT> coord_total_useless_age_bins{};
  std::array<uint64_t, 3> coord_window_bucket_issued{};
  std::array<uint64_t, 3> coord_window_bucket_fill{};
  std::array<uint64_t, 3> coord_window_bucket_useful{};
  std::array<uint64_t, 3> coord_window_bucket_useless{};
  std::array<uint64_t, COORD_AGE_BIN_COUNT> coord_window_fill_age_bins{};
  std::array<uint64_t, COORD_AGE_BIN_COUNT> coord_window_useful_age_bins{};
  std::array<uint64_t, COORD_AGE_BIN_COUNT> coord_window_useless_age_bins{};
  std::array<uint64_t, 3> coord_prev_window_bucket_issued{};
  std::array<uint64_t, 3> coord_prev_window_bucket_fill{};
  std::array<uint64_t, 3> coord_prev_window_bucket_useful{};
  std::array<uint64_t, 3> coord_prev_window_bucket_useless{};
  std::array<uint64_t, COORD_AGE_BIN_COUNT> coord_prev_window_fill_age_bins{};
  std::array<uint64_t, COORD_AGE_BIN_COUNT> coord_prev_window_useful_age_bins{};
  std::array<uint64_t, COORD_AGE_BIN_COUNT> coord_prev_window_useless_age_bins{};
  uint64_t coord_mini_epoch_count = 0;
  uint64_t coord_accesses_in_mini_epoch = 0;
  uint8_t coord_issue_tag_current = 0;
  uint64_t coord_last_miss_latency_cycles = 0;
  uint64_t coord_last_window_miss_latency_delta = 0;
  bool coord_streaming_core_active = false;
  int coord_selected_boost_core_current = -1;
  int coord_phase_core_current = -1;
  uint64_t coord_selected_boost_windows = 0;
  uint64_t coord_phase_owner_windows = 0;
  uint64_t coord_streaming_mini_epochs = 0;
  uint64_t coord_dynrunway_keep3_windows = 0;
  uint64_t coord_dynrunway_midwide_windows = 0;
  uint32_t coord_epoch_base_pf_threshold = 25;
  uint32_t coord_epoch_base_fill_threshold = 90;
  uint32_t coord_epoch_base_issue_period = 1;
  uint32_t coord_epoch_base_keep_candidates = SPP_BWC_HEADGATE_KEEP_CANDIDATES;
  uint32_t coord_epoch_base_mid_max_candidates = SPP_BWC_HEADGATE_MID_MAX_CANDIDATES;
  bool coord_epoch_base_keep_tail = false;

  void record_pressure_sample();
  void finalize_pressure_epoch();
  [[nodiscard]] bool bwc_should_issue();
  void bwc_update_epoch();
  void coord_finalize_mini_epoch(bool flush_partial = false);
  void coord_update_runtime_assignment(bool count_window_assignment);
  void coord_apply_runtime_actuator();
  void coord_record_issued(uint8_t rank_bucket);
  void coord_record_fill(uint32_t metadata_in, uint8_t prefetch);
  void coord_record_useful(uint32_t metadata_in);
  void coord_record_useless(uint32_t metadata_in);
  [[nodiscard]] uint8_t utility_rank_bucket(uint32_t candidate_rank) const;
  [[nodiscard]] static uint32_t make_utility_key(uint32_t signature, uint8_t rank_bucket);
  [[nodiscard]] static uint32_t encode_prefetch_metadata(uint8_t issuer_cpu, uint8_t rank_bucket, uint8_t issue_tag);
  [[nodiscard]] static prefetch_metadata_fields decode_prefetch_metadata(uint32_t metadata);
  [[nodiscard]] static uint8_t metadata_age_bin(uint8_t current_tag, uint8_t issue_tag);
  [[nodiscard]] bool utility_allows_prefetch(uint32_t signature, uint8_t rank_bucket) const;
  [[nodiscard]] int utility_score_or_default(uint32_t signature, uint8_t rank_bucket) const;
  [[nodiscard]] bool utility_allows_l2_fill(uint32_t signature, uint8_t rank_bucket) const;
  void utility_record_prefetch(champsim::address addr, uint32_t signature, uint8_t rank_bucket, uint32_t metadata);
  void utility_record_useful(champsim::address addr);
  void utility_record_useless(champsim::address addr);
  void utility_update_score(uint32_t utility_key, int delta);
  // ────────────────────────────────────────────────────────────────────────────
};

#endif
