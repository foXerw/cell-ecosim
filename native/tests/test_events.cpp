#include "check.h"

#include <cstring>
#include <vector>

#include "core/events.h"
#include "core/world.h"
#include "ecosim_abi.h"

namespace {

constexpr int32_t kCells = 1000;  // 5% outbreak level = 50, 4% reset = 40

void feed(ecosim::EventQueue& q, uint64_t gen, int32_t species, int32_t count) {
  int32_t counts[ECOSIM_MAX_SPECIES] = {};
  counts[species] = count;
  q.observe(gen, counts, kCells);
}

int32_t count_kind(ecosim::EventQueue& q, int32_t kind) {
  std::vector<EcosimEvent> buffer(64);
  int32_t taken = q.poll(buffer.data(), 64);
  int32_t matches = 0;
  for (int32_t i = 0; i < taken; ++i) {
    if (buffer[i].kind == kind) ++matches;
  }
  return matches;
}

}  // namespace

ECOSIM_TEST(outbreak_fires_once_until_it_resets_below_the_hysteresis) {
  ecosim::EventQueue q;
  feed(q, 1, 2, 10);   // below the level
  CHECK_EQ_i32(1, count_kind(q, ECOSIM_EVENT_APPEARANCE));

  feed(q, 2, 2, 60);   // crosses 5% -> outbreak
  CHECK_EQ_i32(1, count_kind(q, ECOSIM_EVENT_OUTBREAK));

  // Hovering around the threshold must not spam the log.
  feed(q, 3, 2, 49);
  feed(q, 4, 2, 61);
  feed(q, 5, 2, 55);
  CHECK_EQ_i32(0, count_kind(q, ECOSIM_EVENT_OUTBREAK));

  // Drop below 4% and climb back: now it is a new outbreak.
  feed(q, 6, 2, 30);
  feed(q, 7, 2, 70);
  CHECK_EQ_i32(1, count_kind(q, ECOSIM_EVENT_OUTBREAK));
}

ECOSIM_TEST(extinction_and_appearance_are_reported_on_the_edge_only) {
  ecosim::EventQueue q;
  feed(q, 1, 3, 5);
  CHECK_EQ_i32(1, count_kind(q, ECOSIM_EVENT_APPEARANCE));

  feed(q, 2, 3, 4);
  feed(q, 3, 3, 3);
  CHECK_EQ_i32(0, count_kind(q, ECOSIM_EVENT_EXTINCTION));

  feed(q, 4, 3, 0);
  CHECK_EQ_i32(1, count_kind(q, ECOSIM_EVENT_EXTINCTION));

  feed(q, 5, 3, 0);  // staying dead is not news every generation
  feed(q, 6, 3, 0);
  CHECK_EQ_i32(0, count_kind(q, ECOSIM_EVENT_EXTINCTION));
}

ECOSIM_TEST(overflow_drops_the_oldest_and_poll_is_partial) {
  ecosim::EventQueue q(4);
  for (int32_t gen = 1; gen <= 10; ++gen) {
    feed(q, static_cast<uint64_t>(gen), 1, gen % 2 == 0 ? 5 : 0);  // alternating edges
  }
  CHECK_TRUE(q.pending() <= 4);

  EcosimEvent one{};
  one.struct_size = sizeof(one);
  int32_t taken = q.poll(&one, 1);
  CHECK_EQ_i32(1, taken);
  CHECK_TRUE(q.pending() >= 1);

  std::vector<EcosimEvent> rest(8);
  taken = q.poll(rest.data(), 8);
  for (int32_t i = 1; i < taken; ++i) {
    CHECK_TRUE(rest[i].generation >= rest[i - 1].generation);
  }
  CHECK_EQ_i32(0, q.pending());
}

ECOSIM_TEST(the_engine_reports_a_real_extinction_through_the_abi) {
  EcosimConfig cfg{};
  cfg.struct_size = sizeof(cfg);
  cfg.w = 8;
  cfg.h = 8;
  cfg.mode = ECOSIM_MODE_ECOSYSTEM;
  cfg.topology = ECOSIM_TOPOLOGY_BOUNDED;
  cfg.threads = 1;
  cfg.rng_seed = 2;
  EcosimWorld* w = ecosim_world_create(&cfg);
  CHECK_TRUE(w != nullptr);

  EcosimSeedConfig seed{};
  seed.struct_size = sizeof(seed);
  seed.species = 2;  // grazers with nothing to eat
  seed.density_permille = 300;
  seed.seed = 2;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_randomize(w, &seed));

  EcosimEvent events[32];
  for (auto& e : events) e.struct_size = sizeof(EcosimEvent);

  int32_t extinctions = 0;
  for (int32_t round = 0; round < 20; ++round) {
    CHECK_EQ_i32(ECOSIM_OK, ecosim_world_step(w, 20));
    int32_t taken = 0;
    CHECK_EQ_i32(ECOSIM_OK, ecosim_event_poll(w, events, 32, &taken));
    for (int32_t i = 0; i < taken; ++i) {
      if (events[i].kind == ECOSIM_EVENT_EXTINCTION) ++extinctions;
    }
  }
  CHECK_TRUE(extinctions >= 1);

  // A fresh poll after the queue drained reports nothing, and bad arguments
  // are refused rather than crashing.
  int32_t taken = 1;
  CHECK_EQ_i32(ECOSIM_OK, ecosim_event_poll(w, events, 32, &taken));
  CHECK_EQ_i32(0, taken);
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_event_poll(w, events, 32, nullptr));
  CHECK_EQ_i32(ECOSIM_ERR_INVALID, ecosim_event_poll(nullptr, events, 32, &taken));

  ecosim_world_destroy(w);
}
