#pragma once

#include <cmath>
#include <string>

namespace commute_sa {

enum class Relation {
    kInside = 0,
    kNear = 1,
    kOutside = 2,
    kUnknown = 3,
};

inline const char *RelationToString(Relation r)
{
    switch (r) {
        case Relation::kInside:
            return "INSIDE";
        case Relation::kNear:
            return "NEAR";
        case Relation::kOutside:
            return "OUTSIDE";
        default:
            return "UNKNOWN";
    }
}

inline double HaversineM(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double kR = 6371000.0;
    constexpr double kPi = 3.14159265358979323846;
    const double p1 = lat1 * kPi / 180.0;
    const double p2 = lat2 * kPi / 180.0;
    const double dlat = (lat2 - lat1) * kPi / 180.0;
    const double dlon = (lon2 - lon1) * kPi / 180.0;
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
        std::cos(p1) * std::cos(p2) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2.0 * kR * std::asin(std::sqrt(a));
}

/** Returns (relation, distance_m). */
inline Relation RelationToAnchor(double lat, double lon, double anchorLat, double anchorLon, double rInM,
    double rOutM, double *distOut)
{
    const double d = HaversineM(lat, lon, anchorLat, anchorLon);
    if (distOut != nullptr) {
        *distOut = d;
    }
    if (d <= rInM) {
        return Relation::kInside;
    }
    if (d < rOutM) {
        return Relation::kNear;
    }
    return Relation::kOutside;
}

}  // namespace commute_sa
