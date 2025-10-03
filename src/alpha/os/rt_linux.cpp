#if defined(__linux__)

#include "alpha/os/rt.hpp"
#include <pthread.h>
#include <sched.h>

namespace alpha::os {

/**
 * @brief Pin current thread to a CPU (if cpu >= 0). Best-effort.
 */
static bool set_affinity(int cpu) {
    if (cpu < 0) return true; // nothing to do
    cpu_set_t mask; CPU_ZERO(&mask); CPU_SET(cpu, &mask);
    return pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) == 0;
}

static bool set_sched(RtSchedPolicy pol, int prio) {
    const int policy = (pol == RtSchedPolicy::RoundRobin) ? SCHED_RR : SCHED_FIFO;
    sched_param sp{}; sp.sched_priority = prio;
    return pthread_setschedparam(pthread_self(), policy, &sp) == 0;
}

bool bind_and_prioritize(const RtConfig& cfg) {
    bool ok = true;
    // 1) Affinity first to avoid migrating to another CPU after becoming RT.
    ok = ok && set_affinity(cfg.cpu);
    // 2) Then set policy/priority (may require privileges).
    ok = ok && set_sched(cfg.policy, cfg.priority);
    return ok;
}

} // namespace alpha::os
#endif
