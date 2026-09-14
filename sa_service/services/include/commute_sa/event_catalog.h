#pragma once

#include "commute_sa/leave_hsmm.h"
#include <cmath>
#include <string>
#include <vector>

namespace commute_sa {

enum class EventTruth { Unknown, False, True };
struct EventDefinition {
    const char *id;
    const char *signal;
    const char *availability;
};

// Facts have no intrinsic polarity or allowed role. Effects belong to patterns.
inline const std::vector<EventDefinition> &EventCatalog()
{
    static const std::vector<EventDefinition> catalog {
        {"walking", "motion", "semantic tick; legacy input has no explicit validity"},
        {"pdr_outbound", "pdr", "semantic tick; legacy input has no explicit validity"},
        {"geo_outbound", "gps", "fresh reliable distinct fix pair; repeated cached fix is unknown"},
        {"wifi_detach", "wifi", "semantic tick; legacy input has no explicit validity"},
        {"cell_detach", "cell", "semantic tick; legacy input has no explicit validity"},
        {"ble_detach", "ble", "semantic tick; legacy input has no explicit validity"},
        {"baro_descending", "baro", "baro_available"},
        {"lower_platform", "baro", "baro_available"},
        {"baro_ascending", "baro", "baro_available"},
        {"vertical_closure", "baro", "baro_available"},
        {"outside", "relation", "relation_known"},
        {"approaching", "relation", "relation_known"},
        {"attached", "wifi", "semantic tick; legacy input has no explicit validity"},
        {"no_baro_descent", "baro", "baro_available; low descending and no lower platform"},
        {"no_geo_outbound", "gps", "same validity as geo_outbound; low evidence does not prove stationarity"}
    };
    return catalog;
}

inline EventTruth EvaluateEventFact(const std::string &event, const LeaveObservation &o)
{
    const auto truth = [](double v, double threshold) {
        return !std::isfinite(v) ? EventTruth::Unknown :
            v >= threshold ? EventTruth::True : EventTruth::False;
    };
    const bool gps = o.geo_observation_known && o.geo_fix_age_s >= 0 &&
        o.geo_fix_age_s <= 30 && o.geo_fix_interval_s > 0 &&
        o.geo_fix_interval_s <= 120 && std::isfinite(o.geo_reliability) && o.geo_reliability >= 0.5 &&
        std::isfinite(o.geo_outbound);
    if (event == "geo_outbound")
        return gps ? truth(o.geo_outbound, .5) : EventTruth::Unknown;
    if (event == "no_geo_outbound")
        return gps ? (o.geo_outbound < .25 ? EventTruth::True : EventTruth::False) : EventTruth::Unknown;
    if (event == "walking") return truth(o.walking, .5);
    if (event == "pdr_outbound") return truth(o.pdr_outbound, .5);
    if (event == "wifi_detach") return truth(o.wifi_detach, .5);
    if (event == "cell_detach") return truth(o.cell_detach, .5);
    if (event == "ble_detach") return truth(o.ble_detach, .5);
    if (event == "attached") return o.attached ? EventTruth::True : EventTruth::False;
    if (event == "outside" || event == "approaching") {
        if (!o.relation_known) return EventTruth::Unknown;
        return (event == "outside" ? o.outside : o.approaching) ? EventTruth::True : EventTruth::False;
    }
    if (!o.baro_available) return EventTruth::Unknown;
    if (event == "baro_descending") return truth(o.baro_descending, .5);
    if (event == "lower_platform") return truth(o.baro_lower_platform, .5);
    if (event == "baro_ascending") return truth(o.baro_ascending, .5);
    if (event == "vertical_closure") return truth(o.vertical_closure, .5);
    if (event == "no_baro_descent") {
        if (!std::isfinite(o.baro_descending) || !std::isfinite(o.baro_lower_platform))
            return EventTruth::Unknown;
        return o.baro_descending < .25 && o.baro_lower_platform < .5 ? EventTruth::True : EventTruth::False;
    }
    return EventTruth::Unknown;
}

// Only simultaneous conjunctions are contradictory. A then not-A is valid in a sequence.
inline bool EventConjunctionValid(const std::vector<std::string> &events)
{
    const auto has = [&](const char *id) {
        for (const auto &e : events) if (e == id) return true;
        return false;
    };
    return !(has("geo_outbound") && has("no_geo_outbound")) &&
        !(has("no_baro_descent") && (has("baro_descending") || has("lower_platform")));
}
}  // namespace commute_sa
