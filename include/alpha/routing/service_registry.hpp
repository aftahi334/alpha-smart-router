#pragma once
// Alpha Smart Router — ServiceRegistry
// Concurrency Model: RCU (Read-Copy-Update) via atomic shared_ptr snapshot swap.
//   • Read-mostly workload: readers take a snapshot (shared_ptr copy) with ACQUIRE semantics.
//   • Writers perform copy-on-write of the whole map and atomically swap with RELEASE semantics.
//   • Readers never block writers; writers never block readers.
//   • Grace period / reclamation is handled by shared_ptr refcounts (no hazard pointers needed).
// Runtime policy: C++23, no exceptions in hot paths, bounded memory (capacity limits).


#include <atomic>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "alpha/routing/pop.hpp"

namespace alpha::routing {

// -----------------------------------------------------------------------------
// Error codes returned by registry operations. Never throw exceptions in hot path.
// -----------------------------------------------------------------------------
/// Result codes for registry mutations.
enum class RegistryErr {
    Ok,         ///< Operation succeeded.
    Exists,     ///< Add failed because service already exists.
    NotFound,   ///< Replace failed because service not found.
    Invalid,    ///< Input validation failed (IDs, IPs, duplicates, limits).
    Capacity    ///< Operation rejected due to configured capacity limits.
};

// -----------------------------------------------------------------------------
// Hard limits for bounded memory usage (RT/embedded friendly).
// -----------------------------------------------------------------------------
/// Compile-time capacity and field limits.
struct Limits {
    static constexpr std::size_t MaxServices         = 128;  ///< Max number of services.
    static constexpr std::size_t MaxPopsPerService   = 32;   ///< Max PoPs per service.
    static constexpr std::size_t MaxIdLen            = 32;   ///< Max length for service_id / pop_id.
    static constexpr std::size_t MaxRegionLen        = 32;   ///< Max length for region strings.
    static constexpr std::size_t MaxIpLen            = 64;   ///< Max length for textual IPs.
};

// -----------------------------------------------------------------------------
// ServiceRegistry class
// -----------------------------------------------------------------------------
///
/// Maintains a mapping: ServiceId → list of PoPs.
/// - Read-mostly workload: optimized with snapshot-swap (RCU-like).
/// - Writes: copy-on-write full map, atomic swap, version increment.
/// - Reads: grab shared_ptr snapshot, consistent, non-blocking.
/// - Bounded: rejects operations exceeding capacity.
/// - Non-throwing: returns RegistryErr codes, never exceptions.
///
/// Thread-safety:
///   - Reads are lock-free and wait-free.
///   - Writes are serialized per call, may allocate.
///   - Readers may see slightly stale data, but always consistent.
//
class ServiceRegistry final {
public:
    // --------------------------- Keying model --------------------------------
    // Transparent hash/equal functors enable heterogeneous lookup with string_view
    // (avoids constructing std::string temporaries for every query).
    struct SKeyHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };
    struct SKeyEq {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept {
            return a == b;
        }
    };

    using Map = std::unordered_map<std::string, PopList, SKeyHash, SKeyEq>;

    // --------------------------- RCU Snapshot API ----------------------------
    /// Return a consistent snapshot of the entire registry map.
    /// Readers must copy the shared_ptr, then access freely without locking.
    std::shared_ptr<const Map> snapshot() const noexcept;

    /// Return a COPY of the PoPs for a service (safe across snapshot swaps).
    [[nodiscard]] PopList getPopsCopy(std::string_view service_id) const noexcept;

    /// Return PoPs converted to a "Pop-like" type (must have {id,region,ip} fields).
    /// Useful for direct interop with IngressSelector::Pop.
    template <class PopLike>
    [[nodiscard]] std::vector<PopLike> getPopsAs(std::string_view service_id) const;

    // --------------------------- Read utilities ------------------------------
    [[nodiscard]] bool hasService(std::string_view service_id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::vector<std::string> listServices() const;

    /// Monotonic version counter. Increments on every successful mutation.
    [[nodiscard]] uint64_t version() const noexcept { return version_.load(std::memory_order_relaxed); }

    // --------------------------- Mutations -----------------------------------
    /// Add new service. Fails if service already exists or data invalid.
    RegistryErr addService(std::string_view service_id, std::span<const Pop> pops);

    /// Replace PoPs for existing service. Fails if service not found or invalid.
    RegistryErr replaceService(std::string_view service_id, std::span<const Pop> pops);

    /// Insert or replace service. Always succeeds if input is valid.
    RegistryErr upsertService(std::string_view service_id, std::span<const Pop> pops);

    /// Remove a service. Returns true if service was erased.
    bool removeService(std::string_view service_id) noexcept;

    /// Clear all services. Treated as maintenance operation.
    void clear() noexcept;

    // Adapters for "Pop-like" inputs (e.g. IngressSelector::Pop).
    template <class PopLike>
    RegistryErr addService(std::string_view service_id, std::span<const PopLike> pops_like);

    template <class PopLike>
    RegistryErr replaceService(std::string_view service_id, std::span<const PopLike> pops_like);

    template <class PopLike>
    RegistryErr upsertService(std::string_view service_id, std::span<const PopLike> pops_like);

    /// Legacy convenience for call-sites expecting bool (deprecated).
    template <class PopLike>
    bool addServiceBool(std::string_view service_id, std::span<const PopLike> pops_like);

    // --------------------------- Observability -------------------------------
    /// Stats counters (atomic, cumulative since start).
    struct Stats {
        uint64_t adds{0}, replaces{0}, upserts{0}, removes{0}, failures{0};
    };
    [[nodiscard]] Stats stats() const noexcept;

private:
    // Current snapshot of registry map (shared_ptr for RCU semantics).
    std::shared_ptr<const Map> map_{std::make_shared<Map>()};
    std::atomic<uint64_t> version_{0};

    // Counters for observability.
    std::atomic<uint64_t> adds_{0}, replaces_{0}, upserts_{0}, removes_{0}, failures_{0};

    // Validation helpers
    static bool validateId(std::string_view id, std::size_t maxLen) noexcept;
    static bool validateIp(std::string_view ip) noexcept;
    static bool validatePops(const PopList& pops) noexcept;

    // Internal mode for mutation routing.
    enum class Mode { Add, Replace, Upsert };

    template <class PopLike>
    RegistryErr addOrReplaceAdapter(Mode mode, std::string_view service_id, std::span<const PopLike> pops_like);

    RegistryErr mutate(Mode mode, std::string_view service_id, const PopList& pops);
};

} // namespace alpha::routing
