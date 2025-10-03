/**
* @file observability.cpp
 * @brief Basic printf-backed implementation of Observer for bring-up.
 */
#include "alpha/obs/observability.hpp"
#include <mutex>
#include <cstdio>

namespace alpha::obs {

    class SimpleObserver : public Observer {
    public:
        void record(const DecisionEvent& e) override {
            std::lock_guard<std::mutex> lk(mu_);
            ctr_.decisions++;
            if (e.reason.find("failover") != std::string::npos) ctr_.failover_triggers++;
            if (!e.scored.empty()) {
                for (const auto& s : e.scored) {
                    if (!s.within_thresholds && s.path_id == e.selected_path) {
                        ctr_.degraded_choices++;
                        break;
                    }
                }
            }
            // JSON-ish line (swap for structured logger later)
            std::printf(
              R"({"decision_id":"%s","path":"%s","score":%.3f,"reason":"%s"})" "\n",
              e.decision_id.c_str(), e.selected_path.c_str(), e.best_score, e.reason.c_str());
            std::fflush(stdout);
        }
        Counters snapshot() const override {
            std::lock_guard<std::mutex> lk(mu_);
            return ctr_;
        }
    private:
        mutable std::mutex mu_;
        Counters ctr_;
    };

    Observer* make_simple_observer() {
        static SimpleObserver obs; // process-wide singleton
        return &obs;
    }

} // namespace alpha::obs