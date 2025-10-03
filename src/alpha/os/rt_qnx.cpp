#if defined(__QNXNTO__)

#include "alpha/os/rt.hpp"
#include <pthread.h>
#include <sched.h>

namespace alpha::os {

/**
 * @brief Apply RT scheduling policy/priority to current thread.
 * Mapping identical to Linux: FIFO/RR -> SCHED_FIFO/SCHED_RR.
 */
static bool set_sched(RtSchedPolicy pol, int prio) {
    const int policy = (pol == RtSchedPolicy::RoundRobin) ? SCHED_RR : SCHED_FIFO;
    sched_param sp{}; sp.sched_priority = prio;
    return pthread_setschedparam(pthread_self(), policy, &sp) == 0;
}

/**
 * @brief CPU affinity (QNX): Not implemented yet.
 * Typical approach is using ThreadCtl(_NTO_TCTL_RUNMASK, ...) to set a CPU run mask.
 * For clarity, we return false when a specific CPU is requested.
 */
static bool set_affinity(int cpu) {
    if (cpu < 0) return true; // "no pin" is OK
    return false; // TODO: implement QNX CPU affinity
}

bool bind_and_prioritize(const RtConfig& cfg) {
    bool ok = true;
    ok = ok && set_affinity(cfg.cpu);            // currently may fail if cpu>=0
    ok = ok && set_sched(cfg.policy, cfg.priority);
    return ok;
}

} // namespace alpha::os
#endif
