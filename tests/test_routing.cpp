/**
 * @file test_routing.cpp
 * @brief Tests for ServiceRegistry RCU semantics + heterogeneous lookup.
 *
 * Validates:
 *  - Snapshot publication via atomic_load/store on shared_ptr (RCU pattern)
 *  - addService / upsertService / replaceService / removeService behavior
 *  - Heterogeneous lookup with std::string_view keys
 *  - No torn reads under 1 writer / many readers
 */

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <string_view>
#include <chrono>

#include "alpha/routing/pop.hpp"
#include "alpha/routing/service_registry.hpp"

using namespace std::chrono_literals;
using alpha::routing::Pop;
using alpha::routing::PopList;
using alpha::routing::ServiceRegistry;

///
/// Helpers: lightweight wrappers so we can pass PopList to span<const Pop>.
///
static inline std::span<const Pop> as_span(const PopList& v) noexcept {
  return std::span<const Pop>(v.data(), v.size());
}

// --------------------------- Basic construction ----------------------------

/**
 * @test Registry_Construct_Empty
 * @brief Fresh registry publishes a valid empty snapshot.
 */
TEST(ServiceRegistry, Registry_Construct_Empty) {
  ServiceRegistry reg;

  auto snap = reg.snapshot();
  ASSERT_TRUE(snap);
  EXPECT_TRUE(snap->empty());
}

// --------------------------- Add / Upsert / Replace ------------------------

/**
 * @test Registry_Add_And_Snapshot
 * @brief Add service with two PoPs, verify snapshot via heterogeneous find.
 */
TEST(ServiceRegistry, Registry_Add_And_Snapshot) {
  ServiceRegistry reg;

  Pop a{.id="nyc", .region="us-east", .ip="192.0.2.10", .weight=100};
  Pop b{.id="sfo", .region="us-west", .ip="198.51.100.20", .weight=100};
  PopList pops{a, b};

  (void)reg.addService("svc1", as_span(pops));

  auto snap = reg.snapshot();
  ASSERT_TRUE(snap);
  auto it = snap->find(std::string_view{"svc1"});   // hetero lookup
  ASSERT_NE(it, snap->end());
  ASSERT_EQ(it->second.size(), 2u);
  EXPECT_EQ(it->second[0], a);
  EXPECT_EQ(it->second[1], b);
}

/**
 * @test Registry_Upsert_Idempotent
 * @brief Upserting identical content should not change logical contents.
 */
TEST(ServiceRegistry, Registry_Upsert_Idempotent) {
  ServiceRegistry reg;

  PopList pops{
    Pop{.id="a1", .region="r1", .ip="203.0.113.1"},
    Pop{.id="a2", .region="r1", .ip="203.0.113.2"},
  };

  (void)reg.upsertService("svc", as_span(pops));
  auto s1 = reg.snapshot();
  ASSERT_TRUE(s1);
  auto it1 = s1->find(std::string_view{"svc"});
  ASSERT_NE(it1, s1->end());

  // Upsert same content
  (void)reg.upsertService("svc", as_span(pops));
  auto s2 = reg.snapshot();
  ASSERT_TRUE(s2);
  auto it2 = s2->find(std::string_view{"svc"});
  ASSERT_NE(it2, s2->end());

  EXPECT_EQ(it1->second, it2->second);  // content equal (pointer identity not required)
}

/**
 * @test Registry_Replace_Content
 * @brief Replace existing content and verify snapshot reflects the change.
 */
TEST(ServiceRegistry, Registry_Replace_Content) {
  ServiceRegistry reg;

  PopList v1{ Pop{.id="xx", .region="rx", .ip="203.0.113.10"} };
  PopList v2{ Pop{.id="yy", .region="ry", .ip="203.0.113.11"} };

  (void)reg.addService("svc", as_span(v1));
  {
    auto s = reg.snapshot();
    auto it = s->find(std::string_view{"svc"});
    ASSERT_NE(it, s->end());
    ASSERT_EQ(it->second.size(), 1u);
    EXPECT_EQ(it->second[0].id, "xx");
  }

  (void)reg.replaceService("svc", as_span(v2));

  auto s2 = reg.snapshot();
  auto it2 = s2->find(std::string_view{"svc"});
  ASSERT_NE(it2, s2->end());
  ASSERT_EQ(it2->second.size(), 1u);
  EXPECT_EQ(it2->second[0].id, "yy");
}

// --------------------------- Remove / Clear --------------------------------

/**
 * @test Registry_Remove_Existing
 * @brief Removing an existing service erases it from the snapshot.
 */
TEST(ServiceRegistry, Registry_Remove_Existing) {
  ServiceRegistry reg;

  PopList pops{ Pop{.id="pp", .region="r", .ip="203.0.113.3"} };
  (void)reg.addService("svcX", as_span(pops));

  // remove and check
  (void)reg.removeService("svcX");
  auto s = reg.snapshot();
  auto it = s->find(std::string_view{"svcX"});
  EXPECT_EQ(it, s->end());
}

/**
 * @test Registry_Remove_NotFound
 * @brief Removing a missing service leaves snapshot unchanged.
 */
TEST(ServiceRegistry, Registry_Remove_NotFound) {
  ServiceRegistry reg;

  auto before = reg.snapshot();
  ASSERT_TRUE(before);
  (void)reg.removeService("does_not_exist");
  auto after = reg.snapshot();
  ASSERT_TRUE(after);

  // map content equal (pointer identity not required)
  EXPECT_EQ(*before, *after);
}

/**
 * @test Registry_Clear
 * @brief Clear removes all services.
 */
TEST(ServiceRegistry, Registry_Clear) {
  ServiceRegistry reg;

  PopList pops{ Pop{.id="p", .region="r", .ip="203.0.113.3"} };
  (void)reg.addService("a", as_span(pops));
  (void)reg.addService("b", as_span(pops));

  reg.clear();

  auto s = reg.snapshot();
  ASSERT_TRUE(s);
  EXPECT_TRUE(s->empty());
}

// --------------------------- Concurrency sanity ----------------------------

/**
 * @test Registry_Concurrency_1W_MR
 * @brief One writer toggles content; readers only observe valid states.
 *
 * This is a lightweight sanity test (not a full linearizability proof).
 */
TEST(ServiceRegistry, Registry_Concurrency_1W_MR) {
  ServiceRegistry reg;

  PopList listA{
    Pop{.id="a1", .region="ra", .ip="203.0.113.1"},
    Pop{.id="a2", .region="ra", .ip="203.0.113.2"},
  };
  PopList listB{
    Pop{.id="b1", .region="rb", .ip="203.0.113.3"},
  };

  std::atomic<bool> running{true};
  std::atomic<int>  ok_reads{0};

  std::thread writer([&]{
    for (int i = 0; i < 4000; ++i) {
      if ((i & 1) == 0) (void)reg.upsertService("svc", as_span(listA));
      else              (void)reg.upsertService("svc", as_span(listB));
      if ((i % 32) == 0) std::this_thread::yield();
    }
    running.store(false, std::memory_order_relaxed);
  });

  auto reader_fn = [&]{
    while (running.load(std::memory_order_relaxed)) {
      auto s = reg.snapshot();
      if (!s) continue;
      auto it = s->find(std::string_view{"svc"});
      if (it != s->end()) {
        const auto n = it->second.size();
        // Must be exactly one of the published sizes; never torn
        if (n == listA.size() || n == listB.size()) {
          ok_reads.fetch_add(1, std::memory_order_relaxed);
        } else {
          ADD_FAILURE() << "Observed invalid size: " << n;
          break;
        }
      }
      std::this_thread::yield();
    }
  };

  std::thread r1(reader_fn), r2(reader_fn), r3(reader_fn);
  writer.join();
  r1.join(); r2.join(); r3.join();

  EXPECT_GT(ok_reads.load(), 0);
}

// --------------------------- validation guard -------------------------------
/**
 * @test Registry_Validation_Rejections_DoNotPublish
 * @brief Invalid inputs must be rejected without publishing a new snapshot.
 *
 * This catches accidental relaxations of validateId/validateIp/duplicate-PoP checks.
 * We do not assert on specific error codes—only on snapshot content.
 */
TEST(ServiceRegistry, Registry_Validation_Rejections_DoNotPublish) {
  ServiceRegistry reg;

  // Start empty
  auto s0 = reg.snapshot();
  ASSERT_TRUE(s0);
  ASSERT_TRUE(s0->empty());

  // 1) Invalid IP (format should fail validation)
  {
    PopList bad_ip{ Pop{.id="ny", .region="r1", .ip="not_an_ip"} };
    (void)reg.addService("svc_bad_ip", as_span(bad_ip));

    auto s = reg.snapshot();
    ASSERT_TRUE(s);
    // No entry should be published
    EXPECT_EQ(s->find(std::string_view{"svc_bad_ip"}), s->end());
  }

  // 2) Duplicate PoP IDs (should fail validation)
  {
    PopList dup_ids{
      Pop{.id="la", .region="r1", .ip="192.0.2.10"},
      Pop{.id="la", .region="r2", .ip="192.0.2.11"} // duplicate id
    };
    (void)reg.addService("svc_dup", as_span(dup_ids));

    auto s = reg.snapshot();
    ASSERT_TRUE(s);
    EXPECT_EQ(s->find(std::string_view{"svc_dup"}), s->end());
  }

  // 3) Positive control: a valid entry does publish
  {
    PopList ok{
      Pop{.id="sf", .region="us-west", .ip="198.51.100.20"},
      Pop{.id="ny", .region="us-east", .ip="192.0.2.30"}
    };
    (void)reg.addService("svc_ok", as_span(ok));

    auto s = reg.snapshot();
    ASSERT_TRUE(s);
    auto it = s->find(std::string_view{"svc_ok"});
    ASSERT_NE(it, s->end());
    EXPECT_EQ(it->second.size(), 2u);
  }
}

