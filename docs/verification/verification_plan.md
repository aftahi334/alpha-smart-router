# Validated Foundations & Routing Semantics – v0.2.0

## Memory Safety & Real-Time Primitives (`tests/test_mem.cpp`)
- **`SpscQueue<T>`**
    - Lock-free, bounded single-producer/single-consumer ring buffer.
    - Explicit slot lifetime management (`std::construct_at`, `std::destroy_at`, destructor/move-assignment `reset()`) validated across wrap-around and partial draining (`SpscQueue.ObjectLifetime_WraparoundDestroyAndMoveAssign`).
    - Pre-allocation validation for power-of-two capacity (`>= 2`), size multiplication overflow, and `std::nothrow` aligned allocation (`SpscQueue.WithCapacity_Validation`).
    - Tested with trivially copyable (`int`), non-trivial lifetime-tracked, and move-only (`std::unique_ptr<int>`) payloads under concurrent producer/consumer threads and AddressSanitizer + UndefinedBehaviorSanitizer.

- **`PacketPool`**
    - Preallocated, recycled packet buffers with power-of-two capacity and `PacketHandle` representability checks (`PacketPool.ConstructAndSeed`, `PacketPool.AcquireRelease_RoundTrip`).
    - Valid handle lifecycle: no heap allocation on the hot path.

## Concurrency, Registry & Seqlocks (`tests/test_routing.cpp`)
- **`ServiceRegistry`**
    - RCU snapshot semantics via atomic `shared_ptr` acquire/release swaps (`ServiceRegistry.Registry_Concurrency_1W_MR`).
    - Heterogeneous `std::string_view` lookup, `upsertService` replacement of existing entries (`Registry_Upsert_ReplacesExistingContent`), and `Limits::MaxServices` capacity enforcement that preserves updates when full (`Registry_Capacity_EnforcesMaxServicesAndAllowsUpdatesWhenFull`).
    - Zero-allocation input validation (`Registry_Validation_Rejections_DoNotPublish`) and observability counters/adapters (`Registry_StatsQueriesAndAdapters`).

- **`MetricsSlot` & `PolicyBinding`**
    - Lock-free atomic payload fields with writer-entry `release` and reader-exit `acquire` fences.
    - Verified under 1-writer / multi-reader contention with zero torn reads (`MetricsSlot.Concurrent_1W_MR_NoTornReads`, `PolicyBinding.PublishSelectAndClear`) and ThreadSanitizer.

## Routing Policies & Data Model (`tests/test_qos.cpp`, `tests/test_ingress.cpp`, `tests/test_failover.cpp`, `tests/test_routing.cpp`)
- **CP ↔ DP Metric Boundary & `QoSPolicy` (`tests/test_qos.cpp`)**
    - Unit-safe conversion between control-plane `QoSPathMetrics` (`loss` in `[0.0, 1.0]`) and data-plane `PathMetrics` / `DpPathMetrics` (`loss_ppm` in `[0, 1,000,000]`) (`RoutingDataModel.*`).
    - Threshold compliance, negative/`NaN` loss rejection, and DSCP-to-`QoSClass` classification and tie-breaking in `LatencyAwarePolicy` (`QoSPolicy.*`).

- **`IngressSelector` & `SimulatedBgpOracle` (`tests/test_ingress.cpp`)**
    - `RoundRobin` over eligible (`Health != Down`) PoPs (`IngressSelector.RoundRobin_CyclesEligiblePopsAndSkipsDown`).
    - Deterministic IPv4/IPv6 client IP hashing (`HashSourceIP`) and 5-tuple flow hashing (`Hash5Tuple`), with `RoundRobin` fallback when hash inputs are absent or malformed (`IngressSelector.Hash*`).
    - `RouteInformed` selection via `SimulatedBgpOracle` tie-breakers with local fallback when routes are missing or locally `Down` (`IngressSelector.RouteInformed_BgpOracleAndFallback`).

- **`FailoverPolicy` (`tests/test_failover.cpp`)**
    - Immediate failover on `HealthState::Down` (`FailoverPolicy.CurrentDown_ImmediateSwitchAndAllDownNullopt`).
    - Primary `recovery_hold_ms` enforced across all branches, including when a recovering primary exceeds `improve_pct_to_switch` (`FailoverPolicy.RecoveryHold_EnforcedEvenWhenPrimaryExceedsMargin`).
    - Selection dwell (`min_hold_ms`) tracked separately from `PathHealth::last_change` (`FailoverPolicy.SelectionDwell_TrackedSeparatelyFromHealthTransitions`), plus configurable `allow_degraded` behavior (`FailoverPolicy.DegradedPolicy_ConfigurableAllowOrReject`).

- **Data-Plane Path Selection Outcomes (`tests/test_routing.cpp`)**
    - Explicit `RouteDecision` and `SelectReason` distinguishing `SelectedHealthy`, `DegradedFallback`, `NoCandidates`, `AllPathsUnhealthy`, and `SnapshotUnavailable` (`PathSelection.OutcomeReasonsAndExplicitDegradedPolicy`).

## Performance Baseline (`benchmarks/spsc_bench.cpp`)
- **`spsc_bench`**
    - Round-trip throughput measured for `push+pop` cycles on trivially copyable and move-only payloads.

