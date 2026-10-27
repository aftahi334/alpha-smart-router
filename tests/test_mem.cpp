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

using alpha::mem::SpscError;
using alpha::mem::SpscQueue;
using alpha::mem::PacketPool;
using alpha::mem::PacketHandle;

// ---------- SpscQueue ----------

TEST(SpscQueue, WithCapacity_Validation) {
  auto bad0 = SpscQueue<int>::with_capacity(0);
  ASSERT_FALSE(bad0.has_value());
  EXPECT_EQ(bad0.error(), SpscError::CapacityZero);

  auto bad1 = SpscQueue<int>::with_capacity(1);
  ASSERT_FALSE(bad1.has_value());
  EXPECT_EQ(bad1.error(), SpscError::CapacityNotPowerOfTwo);

  auto badN = SpscQueue<int>::with_capacity(100);
  ASSERT_FALSE(badN.has_value());
  EXPECT_EQ(badN.error(), SpscError::CapacityNotPowerOfTwo);

  // Highest power-of-two size_t overflows when multiplied by sizeof(uint64_t).
  constexpr std::size_t kMaxPow2 =
      std::size_t{1} << (sizeof(std::size_t) * 8 - 1);
  auto overflow = SpscQueue<std::uint64_t>::with_capacity(kMaxPow2);
  ASSERT_FALSE(overflow.has_value());
  EXPECT_EQ(overflow.error(), SpscError::AllocationFailed);

  struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
    ThrowingMove& operator=(ThrowingMove&&) noexcept(false) { return *this; }
  };
  auto badTrait = SpscQueue<ThrowingMove>::with_capacity(8);
  ASSERT_FALSE(badTrait.has_value());
  EXPECT_EQ(badTrait.error(), SpscError::ElementNotNothrowMovable);

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

namespace {

struct LifetimeTracker {
  static constexpr std::uint32_t kAliveMagic = 0xC0FFEE01u;
  static inline std::atomic<int> live_count{0};

  std::uint32_t magic{kAliveMagic};
  int value{0};

  explicit LifetimeTracker(int v = 0) noexcept : value(v) {
    live_count.fetch_add(1, std::memory_order_relaxed);
  }

  LifetimeTracker(const LifetimeTracker& other) noexcept : value(other.value) {
    EXPECT_EQ(other.magic, kAliveMagic);
    live_count.fetch_add(1, std::memory_order_relaxed);
  }

  LifetimeTracker(LifetimeTracker&& other) noexcept : value(other.value) {
    EXPECT_EQ(other.magic, kAliveMagic);
    other.value = -1;
    live_count.fetch_add(1, std::memory_order_relaxed);
  }

  LifetimeTracker& operator=(const LifetimeTracker& other) noexcept {
    EXPECT_EQ(magic, kAliveMagic);
    EXPECT_EQ(other.magic, kAliveMagic);
    value = other.value;
    return *this;
  }

  LifetimeTracker& operator=(LifetimeTracker&& other) noexcept {
    EXPECT_EQ(magic, kAliveMagic);
    EXPECT_EQ(other.magic, kAliveMagic);
    value = other.value;
    other.value = -1;
    return *this;
  }

  ~LifetimeTracker() noexcept {
    EXPECT_EQ(magic, kAliveMagic);
    magic = 0xDEADDEADu;
    live_count.fetch_sub(1, std::memory_order_relaxed);
  }
};

} // namespace

TEST(SpscQueue, ObjectLifetime_WraparoundDestroyAndMoveAssign) {
  LifetimeTracker::live_count.store(0, std::memory_order_relaxed);

  {
    auto qexp = SpscQueue<LifetimeTracker>::with_capacity(4);
    ASSERT_TRUE(qexp);
    auto q = std::move(*qexp);

    // Fill 3 slots, pop 2, then push 2 more so live items wrap around the ring.
    ASSERT_TRUE(q.push(LifetimeTracker{10}));
    ASSERT_TRUE(q.push(LifetimeTracker{20}));
    ASSERT_TRUE(q.push(LifetimeTracker{30}));
    EXPECT_EQ(LifetimeTracker::live_count.load(), 3);

    {
      LifetimeTracker out{-1};
      ASSERT_TRUE(q.pop(out));
      EXPECT_EQ(out.value, 10);
      ASSERT_TRUE(q.pop(out));
      EXPECT_EQ(out.value, 20);
    }
    // Popped slots must be destroyed immediately.
    EXPECT_EQ(LifetimeTracker::live_count.load(), 1);

    ASSERT_TRUE(q.push(LifetimeTracker{40}));
    ASSERT_TRUE(q.push(LifetimeTracker{50}));
    EXPECT_EQ(LifetimeTracker::live_count.load(), 3);

    // Move-assigning into a non-empty queue must destroy its existing live elements.
    auto q2exp = SpscQueue<LifetimeTracker>::with_capacity(4);
    ASSERT_TRUE(q2exp);
    ASSERT_TRUE(q2exp->push(LifetimeTracker{99}));
    EXPECT_EQ(LifetimeTracker::live_count.load(), 4);

    *q2exp = std::move(q);
    EXPECT_EQ(LifetimeTracker::live_count.load(), 3);
  }

  // Queue destruction must destroy all remaining wrapped elements.
  EXPECT_EQ(LifetimeTracker::live_count.load(), 0);
}


