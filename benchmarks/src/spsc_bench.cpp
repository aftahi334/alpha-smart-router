/**
 * @file spsc_bench.cpp
 * @brief Microbenchmark for SpscQueue<T> (1 producer / 1 consumer).
 *
 * Measures round-trip throughput for `push+pop` pairs using two payload types:
 *   1) `int` (trivially copyable)
 *   2) `std::unique_ptr<int>` (move-only)
 *
 * Reports: items/sec, combined ops/sec (push+pop), and ns per pair.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <barrier>

#include "alpha/mem/spsc_queue.hpp"

namespace bench {
using clock = std::chrono::steady_clock;
using ns    = std::chrono::nanoseconds;

struct Result {
  std::string name;          // e.g., "int@1024"
  std::size_t N = 0;         // number of items transferred
  double      seconds = 0.0; // wall time
  double      items_per_s = 0.0;      // N / seconds
  double      ops_per_s   = 0.0;      // 2N / seconds  (push+pop)
  double      ns_per_pair = 0.0;      // 1e9 * seconds / N
};

// Busy-yield backoff used when ring is full/empty.
inline void backoff() noexcept {
  std::this_thread::yield();
}

// -----------------------------------------------------------------------------
// Core benchmark runner (template on payload type)
// -----------------------------------------------------------------------------

template <class T>
Result run_one(std::string name, std::size_t capacity_pow2, std::size_t N) {
  // Create queue
  auto qexp = alpha::mem::SpscQueue<T>::with_capacity(capacity_pow2);
  if (!qexp) {
    std::cerr << "Failed to create SpscQueue<" << name << "> with capacity "
              << capacity_pow2 << "\n";
    std::abort();
  }
  auto q = std::make_shared<alpha::mem::SpscQueue<T>>(std::move(*qexp));

  // Shared state
  std::barrier sync(2);
  std::atomic<std::size_t> consumed{0};
  clock::time_point t_start, t_end;

  // Producer
  std::thread prod([&] {
    sync.arrive_and_wait();

    for (std::size_t i = 0; i < N;) {
      if constexpr (std::is_same_v<T, int>) {
        if (q->push(static_cast<int>(i))) {
          ++i;
        } else {
          backoff();
        }
      } else {
        // move-only: std::unique_ptr<int>
        T p = std::make_unique<int>(static_cast<int>(i));
        if (q->push(std::move(p))) {
          ++i;
        } else {
          backoff();
        }
      }
    }
  });

  // Consumer
  std::thread cons([&] {
    sync.arrive_and_wait();
    t_start = clock::now();

    if constexpr (std::is_same_v<T, int>) {
      int v{};
      while (consumed.load(std::memory_order_relaxed) < N) {
        if (q->pop(v)) {
          consumed.fetch_add(1, std::memory_order_relaxed);
        } else {
          backoff();
        }
      }
    } else {
      T p;
      while (consumed.load(std::memory_order_relaxed) < N) {
        if (q->pop(p)) {
          p.reset();
          consumed.fetch_add(1, std::memory_order_relaxed);
        } else {
          backoff();
        }
      }
    }

    t_end = clock::now();
  });

  prod.join();
  cons.join();

  const double seconds = std::chrono::duration_cast<ns>(t_end - t_start).count() / 1e9;
  Result r;
  r.name         = std::move(name);
  r.N            = N;
  r.seconds      = seconds;
  r.items_per_s  = (seconds > 0.0) ? (static_cast<double>(N) / seconds) : 0.0;
  r.ops_per_s    = 2.0 * r.items_per_s;
  r.ns_per_pair  = (r.items_per_s > 0.0) ? 1e9 / r.items_per_s : 0.0;
  return r;
}

inline void print(const Result& r) {
  std::cout << std::fixed << std::setprecision(2)
            << std::left << std::setw(18) << r.name
            << "  N=" << std::setw(9) << r.N
            << "  time=" << std::setw(8) << r.seconds << " s"
            << "  items/s=" << std::setw(12) << r.items_per_s
            << "  ops/s="   << std::setw(12) << r.ops_per_s
            << "  ns/pair=" << std::setw(10) << r.ns_per_pair
            << '\n';
}

} // namespace bench

int main() {
  using bench::Result;
  using bench::print;
  using bench::run_one;

  // Parameters
  constexpr std::size_t N  = 1'000'000;   // items per run
  const std::vector<std::size_t> caps = {256, 1024};

  std::cout << "SPSC 1P/1C microbenchmark (push+pop pairs)\n";
  std::cout << "----------------------------------------------------------\n";

  for (auto cap : caps) {
    // int
    auto r1 = run_one<int>("int@" + std::to_string(cap), cap, N);
    print(r1);

    // unique_ptr<int>
    auto r2 = run_one<std::unique_ptr<int>>("uniq_ptr@" + std::to_string(cap), cap, N);
    print(r2);
  }

  std::cout << std::flush;
  return 0;
}
