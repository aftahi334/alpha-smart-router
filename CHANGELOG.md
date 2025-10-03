# 📜 CHANGELOG — Alpha Smart Router

All notable changes to this project will be documented here.  
Format inspired by [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [0.1.0] – Baseline Architecture (October 2025)

### Context
Initial public baseline release after local incubation (Sept 2024 → Oct 2025).  
Focus on establishing a safe, test-driven foundation with memory primitives, concurrency mechanisms, and initial benchmarking.  
See [docs/verification/verification_plan.md](docs/verification/verification_plan.md) for details on validated foundations.

### Added
- **Application (`apps/`)**
  - `router_app`: lightweight scaffold that prints context.
    - Core logic validated via unit tests; pipeline integration planned for future releases.

- **Documentation & Diagrams (`docs/`)**
  - System blueprints: `architecture.svg`, `packet_flow.svg`, `memory_overview.svg` (+ `.mmd` sources).

- **Unit Tests (`tests/`)**
  - Memory primitives (`SpscQueue<T>`, `PacketPool`)
  - Concurrency (`ServiceRegistry` with RCU semantics + heterogeneous lookup)

- **Benchmarks (`benchmarks/`)**
  - `spec_bench`: baseline throughput for `push+pop` cycles on copyable and move-only types.  
