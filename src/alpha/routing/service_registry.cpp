// ServiceRegistry — RCU Implementation Notes
// We implement RCU with shared_ptr snapshots:
//   • Readers: atomic_load (ACQUIRE) → non-blocking, consistent view.
//   • Writers: copy current map, mutate, atomic_store (RELEASE).
// The shared_ptr reference count naturally provides a grace period:
// old snapshots remain alive until the last reader drops its ref, after which
// they are reclaimed automatically (no explicit epoch/hazard management).

#include "alpha/routing/service_registry.hpp"

#include <algorithm>
#include <memory>   // atomic_load/atomic_store for shared_ptr
#include <cassert>
#include <cstring>
#include <string_view>
#include <arpa/inet.h>

namespace alpha::routing {

//------------------------------- Validation -----------------------------------

bool ServiceRegistry::validateId(std::string_view id, std::size_t maxLen) noexcept {
    if (id.empty() || id.size() > maxLen) return false;
    // Allow [A-Za-z0-9_-], 2..maxLen
    if (id.size() < 2) return false;
    for (char c : id) {
        const bool ok = (c == '_' || c == '-' ||
                         (c >= '0' && c <= '9') ||
                         (c >= 'A' && c <= 'Z') ||
                         (c >= 'a' && c <= 'z'));
        if (!ok) return false;
    }
    return true;
}

bool ServiceRegistry::validateIp(std::string_view ip) noexcept {
    if (ip.empty() || ip.size() > Limits::MaxIpLen) return false;
    // Try IPv4 then IPv6
    unsigned char buf[sizeof(struct in6_addr)];
    if (inet_pton(AF_INET,  std::string(ip).c_str(), buf) == 1) return true;
    if (inet_pton(AF_INET6, std::string(ip).c_str(), buf) == 1) return true;
    return false;
}

bool ServiceRegistry::validatePops(const PopList& pops) noexcept {
    if (pops.empty() || pops.size() > Limits::MaxPopsPerService) return false;
    // unique ids, fields non-empty & within limits, valid IP
    std::vector<std::string_view> ids; ids.reserve(pops.size());
    for (const auto& p : pops) {
        if (!validateId(p.id, Limits::MaxIdLen)) return false;
        if (!validateId(p.region, Limits::MaxRegionLen)) return false;
        if (!validateIp(p.ip)) return false;
        ids.push_back(p.id);
    }
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) return false;
    return true;
}

//------------------------------- Public API -----------------------------------

std::shared_ptr<const ServiceRegistry::Map>
ServiceRegistry::snapshot() const noexcept {
    // RCU read: acquire ensures any reader observing the pointer also observes
    // the fully constructed map published with RELEASE in writer path.
    return std::atomic_load_explicit(&map_, std::memory_order_acquire);
}
PopList ServiceRegistry::getPopsCopy(std::string_view service_id) const noexcept {
    auto snap = snapshot();
    if (!snap) return {};
    auto it = snap->find(service_id);
    if (it == snap->end()) return {};
    return it->second; // copy
}

bool ServiceRegistry::hasService(std::string_view service_id) const noexcept {
    auto snap = snapshot();
    return snap && (snap->find(service_id) != snap->end());
}

std::size_t ServiceRegistry::size() const noexcept {
    auto snap = snapshot();
    return snap ? snap->size() : 0;
}

std::vector<std::string> ServiceRegistry::listServices() const {
    std::vector<std::string> out;
    auto snap = snapshot();
    if (!snap) return out;
    out.reserve(snap->size());
    for (const auto& kv : *snap) out.push_back(kv.first);
    return out;
}

RegistryErr ServiceRegistry::addService(std::string_view service_id, std::span<const Pop> pops) {
    PopList list(pops.begin(), pops.end());
    return mutate(Mode::Add, service_id, list);
}

RegistryErr ServiceRegistry::replaceService(std::string_view service_id, std::span<const Pop> pops) {
    PopList list(pops.begin(), pops.end());
    return mutate(Mode::Replace, service_id, list);
}

RegistryErr ServiceRegistry::upsertService(std::string_view service_id, std::span<const Pop> pops) {
    PopList list(pops.begin(), pops.end());
    return mutate(Mode::Upsert, service_id, list);
}

bool ServiceRegistry::removeService(std::string_view service_id) noexcept {
    auto snap = snapshot();
    if (!snap || snap->empty()) return false;

    auto next = std::make_shared<Map>(*snap);
    auto it = next->find(service_id);
    bool erased = (it != next->end());
    if (erased) next->erase(it);
    if (!erased) return false;

    // RCU update: publish new snapshot. RELEASE pairs with reader ACQUIRE so that
    // all prior writes to *next (the new map) are visible to readers that load it.
    std::shared_ptr<const Map> cnext = std::move(next); // convert Map -> const Map
    std::atomic_store_explicit(&map_, std::move(cnext), std::memory_order_release);
    version_.fetch_add(1, std::memory_order_relaxed);
    removes_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void ServiceRegistry::clear() noexcept {
    auto next = std::make_shared<Map>();
    // RCU update: publish new snapshot. RELEASE pairs with reader ACQUIRE so that
    // all prior writes to *next (the new map) are visible to readers that load it.
    std::shared_ptr<const Map> cnext = std::move(next); // convert Map -> const Map
    std::atomic_store_explicit(&map_, std::move(cnext), std::memory_order_release);
    version_.fetch_add(1, std::memory_order_relaxed);
    // Not counting as failure/success here; treated as maintenance op.
}

//------------------------------- Mutation Core --------------------------------

RegistryErr ServiceRegistry::mutate(Mode mode, std::string_view service_id, const PopList& pops) {
    // Basic input checks
    if (!validateId(service_id, Limits::MaxIdLen)) { failures_.fetch_add(1, std::memory_order_relaxed); return RegistryErr::Invalid; }
    if (!validatePops(pops))                        { failures_.fetch_add(1, std::memory_order_relaxed); return RegistryErr::Invalid; }

    auto snap = snapshot();
    if (!snap) { failures_.fetch_add(1, std::memory_order_relaxed); return RegistryErr::Invalid; }

    if (snap->size() > Limits::MaxServices) {
        failures_.fetch_add(1, std::memory_order_relaxed);
        return RegistryErr::Capacity;
    }

    auto next = std::make_shared<Map>(*snap); // copy-on-write
    auto it   = next->find(service_id);
    const bool exists = (it != next->end());

    switch (mode) {
        case Mode::Add:
            if (exists) { failures_.fetch_add(1, std::memory_order_relaxed); return RegistryErr::Exists; }
            // insert (avoids default-construct + assign)
            next->emplace(std::string(service_id), std::move(pops));
            { // publish
                std::shared_ptr<const Map> cnext = std::move(next);
                std::atomic_store_explicit(&map_, std::move(cnext), std::memory_order_release);
            }
            version_.fetch_add(1, std::memory_order_relaxed);
            adds_.fetch_add(1, std::memory_order_relaxed);
            return RegistryErr::Ok;

        case Mode::Replace:
            if (!exists) { failures_.fetch_add(1, std::memory_order_relaxed); return RegistryErr::NotFound; }
            it->second = std::move(pops);
            { // RCU update: publish new snapshot
                std::shared_ptr<const Map> cnext = std::move(next);
                std::atomic_store_explicit(&map_, std::move(cnext), std::memory_order_release);
            }
            version_.fetch_add(1, std::memory_order_relaxed);
            replaces_.fetch_add(1, std::memory_order_relaxed);
            return RegistryErr::Ok;

        case Mode::Upsert:
            next->emplace(std::string(service_id), std::move(pops));
            { // RCU update: publish new snapshot
                std::shared_ptr<const Map> cnext = std::move(next);
                std::atomic_store_explicit(&map_, std::move(cnext), std::memory_order_release);
            }

            version_.fetch_add(1, std::memory_order_relaxed);
            upserts_.fetch_add(1, std::memory_order_relaxed);
            return RegistryErr::Ok;
    }

    failures_.fetch_add(1, std::memory_order_relaxed);
    return RegistryErr::Invalid;
}

} // namespace alpha::routing
