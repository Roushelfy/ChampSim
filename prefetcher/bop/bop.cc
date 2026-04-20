#include "bop.h"

#include <cassert>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// Initialisation
// ─────────────────────────────────────────────────────────────────────────────

void bop::prefetcher_initialize()
{
  std::cout << "Initialize BOP" << std::endl;
  std::cout << "  NUM_OFFSETS = " << NUM_OFFSETS << std::endl;
  std::cout << "  SCORE_MAX   = " << SCORE_MAX   << std::endl;
  std::cout << "  ROUND_MAX   = " << ROUND_MAX   << std::endl;
  std::cout << "  BAD_SCORE   = " << BAD_SCORE   << std::endl;
  std::cout << "  RR banks    = 2 x " << (1 << RRINDEX) << " (RRINDEX=" << RRINDEX << ", RRTAG=" << RRTAG << ")" << std::endl;
  std::cout << "  DQ size     = " << DELAYQSIZE << " (delay=" << DELAY << " cycles)" << std::endl;

  rr_left.fill(0);
  rr_right.fill(0);

  for (auto& e : dq) { e = {0, 0, false}; }
  dq_head = 0;
  dq_tail = 0;

  score.fill(0);
  os_max_score   = 0;
  os_best_offset = 0;
  os_round       = 0;
  os_p           = 0;

  prefetch_offset = DEFAULT_OFFSET;
  bop_cycle       = 0;
  total_pf_issued = 0;
  total_pf_useful = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cycle tick — advance proxy cycle counter and nothing else
// ─────────────────────────────────────────────────────────────────────────────

void bop::prefetcher_cycle_operate()
{
  ++bop_cycle;
}

// ─────────────────────────────────────────────────────────────────────────────
// Recent Requests Table (2-bank, XOR-hash + tag)
// ─────────────────────────────────────────────────────────────────────────────

uint32_t bop::rr_tag(uint64_t lineaddr) const
{
  // Upper RRTAG bits above the RRINDEX-bit index field
  return static_cast<uint32_t>((lineaddr >> RRINDEX) & ((1u << RRTAG) - 1u));
}

uint32_t bop::rr_idx_left(uint64_t lineaddr) const
{
  return static_cast<uint32_t>((lineaddr ^ (lineaddr >> RRINDEX)) & ((1u << RRINDEX) - 1u));
}

uint32_t bop::rr_idx_right(uint64_t lineaddr) const
{
  return static_cast<uint32_t>((lineaddr ^ (lineaddr >> (2 * RRINDEX))) & ((1u << RRINDEX) - 1u));
}

void bop::rr_insert_left(uint64_t lineaddr)
{
  rr_left[rr_idx_left(lineaddr)] = rr_tag(lineaddr);
}

void bop::rr_insert_right(uint64_t lineaddr)
{
  rr_right[rr_idx_right(lineaddr)] = rr_tag(lineaddr);
}

bool bop::rr_hit(uint64_t lineaddr) const
{
  return (rr_left[rr_idx_left(lineaddr)]   == rr_tag(lineaddr))
      || (rr_right[rr_idx_right(lineaddr)] == rr_tag(lineaddr));
}

// ─────────────────────────────────────────────────────────────────────────────
// Delay Queue
// ─────────────────────────────────────────────────────────────────────────────

uint32_t bop::trunc_cycle(uint64_t c) const
{
  return static_cast<uint32_t>(c & ((1u << TIME_BITS) - 1u));
}

// Push a demand lineaddr into the DQ (it will move to rr_left after DELAY cycles).
void bop::dq_push(uint64_t lineaddr)
{
  if (dq[dq_tail].valid) {
    // DQ is full — evict oldest entry immediately to rr_left
    rr_insert_left(dq[dq_head].lineaddr);
    dq[dq_head].valid = false;
    dq_head = (dq_head + 1) % DELAYQSIZE;
  }
  dq[dq_tail] = {lineaddr, trunc_cycle(bop_cycle), true};
  dq_tail = (dq_tail + 1) % DELAYQSIZE;
}

// Drain all DQ entries whose DELAY has elapsed into rr_left.
void bop::dq_pop()
{
  while (dq[dq_head].valid) {
    uint32_t now    = trunc_cycle(bop_cycle);
    uint32_t issued = dq[dq_head].cycle;
    uint32_t ready  = trunc_cycle(issued + static_cast<uint32_t>(DELAY));

    // Wrapped cycle comparison (matches DPC-3 dq_ready logic):
    bool expired;
    if (ready >= issued) {
      expired = (now >= ready) || (now < issued);
    } else {
      // wrapped: ready < issued
      expired = (now >= ready) && (now < issued);
    }

    if (!expired) break;

    rr_insert_left(dq[dq_head].lineaddr);
    dq[dq_head].valid = false;
    dq_head = (dq_head + 1) % DELAYQSIZE;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Offset Scoring (OS)
// ─────────────────────────────────────────────────────────────────────────────

void bop::os_learn_best_offset(uint64_t lineaddr)
{
  int      offset   = OFFSET_LIST[os_p];
  // testlineaddr = lineaddr - offset.  Negative offsets → unsigned wrap.
  // The samepage check below will reject cross-page wraps.
  uint64_t testaddr = lineaddr - static_cast<uint64_t>(offset);

  // Only score if the candidate base address is on the same page
  // and was recently seen (rr_hit).  Uses ChampSim's page granularity:
  // SAMEPAGE(a,b) ≡ ((a^b) >> 6) == 0 (64 blocks per page).
  if (((lineaddr ^ testaddr) >> 6) == 0 && rr_hit(testaddr)) {
    ++score[os_p];
    if (score[os_p] >= os_max_score) {  // >= so later offsets break ties
      os_max_score   = score[os_p];
      os_best_offset = offset;
    }
  }

  // End-of-round check (after testing the last offset in the list)
  if (os_p == NUM_OFFSETS - 1) {
    ++os_round;
    if (os_max_score >= SCORE_MAX || os_round >= ROUND_MAX) {
      // Commit best offset for the next epoch
      prefetch_offset = (os_best_offset != 0) ? os_best_offset : DEFAULT_OFFSET;
      if (os_max_score <= BAD_SCORE) {
        prefetch_offset = 0;  // score too low → disable prefetching
      }
      // Reset for next learning round
      score.fill(0);
      os_max_score   = 0;
      os_best_offset = 0;
      os_round       = 0;
      os_p           = 0;
      return;  // skip the increment below so os_p stays at 0
    }
  }

  os_p = (os_p + 1) % NUM_OFFSETS;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main prefetcher callback — called on every L2C demand access
// ─────────────────────────────────────────────────────────────────────────────

uint32_t bop::prefetcher_cache_operate(champsim::address addr, champsim::address ip,
                                       uint8_t cache_hit, bool useful_prefetch,
                                       access_type type, uint32_t metadata_in)
{
  champsim::block_number blk{addr};
  uint64_t lineaddr = blk.to<uint64_t>();

  if (useful_prefetch)
    ++total_pf_useful;

  // Drain expired DQ entries into rr_left
  dq_pop();

  // Learning and prefetching only on demand miss OR prefetch hit
  // (a clean demand hit on a non-prefetched line carries no new information)
  if (!cache_hit || useful_prefetch) {
    os_learn_best_offset(lineaddr);

    if (prefetch_offset != 0) {
      // Compute prefetch address: lineaddr + prefetch_offset (signed arithmetic)
      auto pf_blk = blk + static_cast<champsim::block_number::difference_type>(prefetch_offset);
      champsim::address pf_addr{pf_blk};

      // Only prefetch within the same physical page (SAMEPAGE check)
      if (champsim::page_number{pf_addr} == champsim::page_number{addr}) {
        // fill_this_level=false → fill to LLC (FILL_LLC), matching DPC-3 reference
        bool issued = prefetch_line(pf_addr, false, encode_prefetch_metadata(static_cast<uint32_t>(prefetch_offset)));
        if (issued) {
          ++total_pf_issued;
          // Push the DEMAND block (not pf_addr) into DQ; it will enter rr_left
          // after DELAY cycles, representing the triggering address for scoring.
          dq_push(lineaddr);
        }
      }
    }
  }

  return metadata_in;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fill callback — inserts triggering address into rr_right
// ─────────────────────────────────────────────────────────────────────────────

uint32_t bop::prefetcher_cache_fill(champsim::address addr, long set, long way,
                                    uint8_t prefetch, champsim::address evicted_addr,
                                    uint32_t metadata_in)
{
  // Insert when: (a) this fill came from a prefetch, or (b) prefetching is
  // currently disabled (to warm up the RRT for when it re-enables).
  if (prefetch || prefetch_offset == 0) {
    uint64_t lineaddr = champsim::block_number{addr}.to<uint64_t>();

    // baselineaddr = fill_addr - prefetch_offset
    // For prefetch fills: the prefetch was issued as (demand_addr + prefetch_offset),
    // so demand_addr = fill_addr - prefetch_offset.
    // We use the current prefetch_offset (same as DPC-3 reference).
    uint64_t baselineaddr = lineaddr - static_cast<uint64_t>(prefetch_offset);

    // Only insert if they're on the same page (catches unsigned wrap-around)
    if (((lineaddr ^ baselineaddr) >> 6) == 0) {
      rr_insert_right(baselineaddr);
    }
  }

  return metadata_in;
}

// ─────────────────────────────────────────────────────────────────────────────
// Final stats
// ─────────────────────────────────────────────────────────────────────────────

void bop::prefetcher_final_stats()
{
  std::cout << "[BOP] prefetch_offset=" << prefetch_offset
            << " total_pf_issued=" << total_pf_issued
            << " total_pf_useful=" << total_pf_useful;
  if (total_pf_issued > 0) {
    std::cout << " accuracy="
              << (100.0 * static_cast<double>(total_pf_useful) /
                  static_cast<double>(total_pf_issued))
              << "%";
  }
  std::cout << std::endl;
}
