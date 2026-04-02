#ifndef BOP_H
#define BOP_H

#include <array>
#include <cstdint>

#include "cache.h"
#include "modules.h"

// Best-Offset Prefetcher (BOP)
// Adapted from the DPC-3 reference implementation by Pierre Michaud (Inria).
// Original: "A Best-Offset Prefetcher", DPC-2 2015 / HPCA 2016.
struct bop : public champsim::modules::prefetcher {

  // ── Algorithm parameters (DPC-3 reference values) ─────────────────────────
  // 40 offsets: positive/negative pairs ±1..±16,±18,±20,±24,±30,±32,±36,±40
  constexpr static int OFFSET_LIST[] = {
    1,-1,2,-2,3,-3,4,-4,5,-5,6,-6,7,-7,8,-8,9,-9,10,-10,
    11,-11,12,-12,13,-13,14,-14,15,-15,16,-16,
    18,-18,20,-20,24,-24,30,-30,32,-32,36,-36,40,-40
  };
  constexpr static int NUM_OFFSETS    = 40;
  constexpr static int DEFAULT_OFFSET = 1;
  constexpr static int SCORE_MAX      = 31;
  constexpr static int ROUND_MAX      = 100;
  constexpr static int BAD_SCORE      = 1;

  // Recent Requests Table: 2 banks of 64 entries, 12-bit tags
  constexpr static int RRINDEX = 6;    // 2^RRINDEX = 64 entries per bank
  constexpr static int RRTAG   = 12;  // bits stored per entry

  // Delay Queue: 15 entries, 60-cycle delay, 12-bit wrapped cycle counter
  constexpr static int DELAYQSIZE = 15;
  constexpr static int DELAY      = 60;
  constexpr static int TIME_BITS  = 12;
  // ─────────────────────────────────────────────────────────────────────────

  using prefetcher::prefetcher;

  void     prefetcher_initialize();
  void     prefetcher_cycle_operate();
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip,
               uint8_t cache_hit, bool useful_prefetch, access_type type,
               uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way,
               uint8_t prefetch, champsim::address evicted_addr,
               uint32_t metadata_in);
  void     prefetcher_final_stats();

  // ── Recent Requests Table (2-bank, XOR-hashed) ────────────────────────────
  std::array<uint32_t, (1 << RRINDEX)> rr_left{};    // left bank: tag per entry
  std::array<uint32_t, (1 << RRINDEX)> rr_right{};   // right bank: tag per entry

  // ── Delay Queue ───────────────────────────────────────────────────────────
  struct DelayEntry {
    uint64_t lineaddr = 0;
    uint32_t cycle    = 0;
    bool     valid    = false;
  };
  std::array<DelayEntry, DELAYQSIZE> dq{};
  int dq_head = 0;
  int dq_tail = 0;

  // ── Offset Score Table ────────────────────────────────────────────────────
  std::array<int, NUM_OFFSETS> score{};
  int os_max_score   = 0;
  int os_best_offset = 0;
  int os_round       = 0;
  int os_p           = 0;   // index into OFFSET_LIST currently being evaluated

  // ── Global prefetch state (non-static → per-L2C, multi-core safe) ─────────
  int prefetch_offset = DEFAULT_OFFSET;  // 0 means prefetching disabled

  // Proxy cycle counter (incremented in prefetcher_cycle_operate)
  uint64_t bop_cycle = 0;

  // ── Statistics ────────────────────────────────────────────────────────────
  uint64_t total_pf_issued = 0;
  uint64_t total_pf_useful = 0;

private:
  // RR helpers
  uint32_t rr_tag(uint64_t lineaddr) const;
  uint32_t rr_idx_left(uint64_t lineaddr) const;
  uint32_t rr_idx_right(uint64_t lineaddr) const;
  void     rr_insert_left(uint64_t lineaddr);
  void     rr_insert_right(uint64_t lineaddr);
  bool     rr_hit(uint64_t lineaddr) const;

  // Delay Queue helpers
  uint32_t trunc_cycle(uint64_t c) const;
  void     dq_push(uint64_t lineaddr);
  void     dq_pop();

  // Scoring
  void os_learn_best_offset(uint64_t lineaddr);
};

#endif
