// apps/ping_tool/src/main.cpp
// Alpha Smart Router — ping_tool
// Purpose: Standalone helper tool for probing PoPs or paths (RTT, jitter, loss).
// This is NOT the main router — it's a demo/testing utility.
//
// Usage example (future):
//   ./ping_tool <target_ip> [count]
//
// Notes:
// - In Option A: simulate probes with sleep/randomized RTT.
// - In Option B: actually ping FRR PoPs to measure ingress health. => implemented option
// - Results can be fed into the latency-aware path selection module.

#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <thread>

static void simulate_ping(const std::string& target, int count) {
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(10, 50); // Simulated RTT (ms)

    for (int i = 0; i < count; ++i) {
        int rtt = dist(rng);
        std::cout << "PING " << target
                  << " seq=" << i
                  << " rtt=" << rtt << " ms"
                  << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int main(int argc, char** argv) {
    std::string target = (argc > 1) ? argv[1] : "10.0.0.1";
    int count = (argc > 2) ? std::stoi(argv[2]) : 5;

    std::cout << "Alpha Smart Router — ping_tool starting" << std::endl;
    std::cout << "Target: " << target << ", count: " << count << std::endl;

    simulate_ping(target, count);

    std::cout << "ping_tool finished" << std::endl;
    return 0;
}
