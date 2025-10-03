/**
 * @file test_mem.cpp
 * @brief Tests for SpscQueue<T> (owning, RT) and PacketPool.
 */
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <chrono>

#include "alpha/mem/packet_pool.hpp"

using alpha::mem::SpscQueue;
using alpha::mem::PacketPool;
using alpha::mem::PacketHandle;

// ---------- SpscQueue ----------

TEST(SpscQueue, WithCapacity_Validation) {
  auto bad0 = SpscQueue<int>::with_capacity(0);
  EXPECT_FALSE(bad0.has_value());
  auto badN = SpscQueue<int>::with_capacity(100);
  EXPECT_FALSE(badN.has_value());
  auto ok = SpscQueue<int>::with_capacity(1024);
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->capacity(), 1024u);
}

TEST(SpscQueue, SingleThread_Basics) {
  constexpr std::size_t CAP = 8;
  auto qexp = SpscQueue<int>::with_capacity(CAP);
  ASSERT_TRUE(qexp);
  auto q = std::move(*qexp);

  // fill N-1
  for (int i = 0; i < int(CAP - 1); ++i) EXPECT_TRUE(q.push(i));
  EXPECT_TRUE(q.full());
  EXPECT_FALSE(q.push(999));

  // pop 3
  for (int i = 0; i < 3; ++i) {
    int v{};
    ASSERT_TRUE(q.pop(v));
    EXPECT_EQ(v, i);
  }

  // push 3 (wrap)
  for (int i = 100; i < 103; ++i) EXPECT_TRUE(q.push(i));

  // drain & check order
  std::vector<int> out;
  int v{};
  while (q.pop(v)) out.push_back(v);
  std::vector<int> expected = {3,4,5,6,100,101,102};
  EXPECT_EQ(out, expected);
  EXPECT_TRUE(q.empty());
}

TEST(SpscQueue, ProducerConsumer_Concurrent) {
  constexpr std::size_t CAP = 1024, N = 50000;
  auto qexp = SpscQueue<std::uint32_t>::with_capacity(CAP);
  ASSERT_TRUE(qexp);
  auto q = std::make_shared<SpscQueue<std::uint32_t>>(std::move(*qexp));

  std::atomic<std::size_t> produced{0}, consumed{0};
  std::thread prod([&]{
    for (std::size_t i = 0; i < N;) {
      if (q->push(static_cast<std::uint32_t>(i))) { ++i; produced.fetch_add(1, std::memory_order_relaxed); }
      else std::this_thread::yield();
    }
  });
  std::vector<std::uint32_t> out; out.reserve(N);
  std::thread cons([&]{
    std::uint32_t v{};
    while (consumed.load(std::memory_order_relaxed) < N) {
      if (q->pop(v)) { out.push_back(v); consumed.fetch_add(1, std::memory_order_relaxed); }
      else std::this_thread::yield();
    }
  });
  prod.join(); cons.join();

  ASSERT_EQ(out.size(), N);
  for (std::size_t i = 0; i < N; ++i) EXPECT_EQ(out[i], i);
  EXPECT_TRUE(q->empty());
}

// ---------- PacketPool ----------

TEST(PacketPool, ConstructAndSeed) {
  constexpr std::size_t CAP = 64;
  PacketPool pool(CAP);

  std::vector<PacketHandle> hs; hs.reserve(CAP);
  for (std::size_t i = 0; i < CAP; ++i) {
    PacketHandle h{};
    ASSERT_TRUE(pool.acquire(h));
    hs.push_back(h);
  }
  PacketHandle tmp{};
  EXPECT_FALSE(pool.acquire(tmp)); // ring empty

  // return all
  for (auto h : hs) EXPECT_TRUE(pool.release(h));
}

TEST(PacketPool, AcquireRelease_RoundTrip) {
  constexpr std::size_t CAP = 8;
  PacketPool pool(CAP);

  // drain
  std::vector<PacketHandle> drained; drained.reserve(CAP);
  for (std::size_t i = 0; i < CAP; ++i) { PacketHandle h{}; ASSERT_TRUE(pool.acquire(h)); drained.push_back(h); }

  // write metadata, release in order
  for (std::size_t i = 0; i < CAP; ++i) {
    auto h = drained[i];
    auto& pkt = pool.get(h);
    pkt.length = 100 + i;
    pkt.meta   = static_cast<std::uint32_t>(i);
    ASSERT_TRUE(pool.release(h));
  }

  // re-acquire and verify same order + metadata
  for (std::size_t i = 0; i < CAP; ++i) {
    PacketHandle h{}; ASSERT_TRUE(pool.acquire(h));
    EXPECT_EQ(h, drained[i]);
    const auto& pkt = pool.get(h);
    EXPECT_EQ(pkt.length, 100 + i);
    EXPECT_EQ(pkt.meta, static_cast<std::uint32_t>(i));
  }
}

/**
 * @test SpscQueue_MoveOnly_ProducerConsumer
 * @brief Verifies SpscQueue works with move-only types (e.g., std::unique_ptr<int>).
 *
 * Why this matters:
 *  - Ensures push/pop are truly move-based (no hidden copies).
 *  - Confirms noexcept move paths and memory orders behave with non-trivial T.
 *  - Catches template/trait mistakes early (common in RT queues).
 */
TEST(SpscQueue, MoveOnly_ProducerConsumer) {
  using Ptr = std::unique_ptr<int>;
  constexpr std::size_t CAP = 256;
  constexpr std::size_t N   = 10000;

  auto qexp = SpscQueue<Ptr>::with_capacity(CAP);
  ASSERT_TRUE(qexp) << "Expected SpscQueue<unique_ptr<int>> to be supported";
  auto q = std::make_shared<SpscQueue<Ptr>>(std::move(*qexp));

  std::atomic<std::size_t> produced{0};
  std::atomic<std::size_t> consumed{0};

  // Producer: creates fresh unique_ptr<int> and pushes them in order 0..N-1
  std::thread prod([&]{
    for (std::size_t i = 0; i < N; ) {
      Ptr p = std::make_unique<int>(static_cast<int>(i));
      if (q->push(std::move(p))) {
        ++i;
        produced.fetch_add(1, std::memory_order_relaxed);
      } else {
        // queue full — back off lightly
        std::this_thread::yield();
      }
    }
  });

  // Consumer: pops into Ptr and records the integer payloads
  std::vector<int> out;
  out.reserve(N);
  std::thread cons([&]{
    Ptr p;
    while (consumed.load(std::memory_order_relaxed) < N) {
      if (q->pop(p)) {
        ASSERT_TRUE(p) << "Popped unique_ptr should be non-null";
        out.push_back(*p);
        p.reset(); // release ownership explicitly
        consumed.fetch_add(1, std::memory_order_relaxed);
      } else {
        // queue empty — back off lightly
        std::this_thread::yield();
      }
    }
  });

  prod.join();
  cons.join();

  ASSERT_EQ(out.size(), N);
  for (std::size_t i = 0; i < N; ++i) {
    EXPECT_EQ(out[i], static_cast<int>(i));
  }
  EXPECT_TRUE(q->empty());
  EXPECT_EQ(produced.load(), N);
  EXPECT_EQ(consumed.load(), N);
}

