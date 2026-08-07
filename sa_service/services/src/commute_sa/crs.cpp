#include "commute_sa/crs.h"

#include <cmath>

namespace commute_sa {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthAxis = 6378245.0;
constexpr double kEccentricity = 0.00669342162296594323;

double TransformLat(double x, double y)
{
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * std::sqrt(std::abs(x));
    ret += (20.0 * std::sin(6.0 * x * kPi) + 20.0 * std::sin(2.0 * x * kPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(y * kPi) + 40.0 * std::sin(y / 3.0 * kPi)) * 2.0 / 3.0;
    ret += (160.0 * std::sin(y / 12.0 * kPi) + 320.0 * std::sin(y * kPi / 30.0)) * 2.0 / 3.0;
    return ret;
}

double TransformLon(double x, double y)
{
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * std::sqrt(std::abs(x));
    ret += (20.0 * std::sin(6.0 * x * kPi) + 20.0 * std::sin(2.0 * x * kPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(x * kPi) + 40.0 * std::sin(x / 3.0 * kPi)) * 2.0 / 3.0;
    ret += (150.0 * std::sin(x / 12.0 * kPi) + 300.0 * std::sin(x / 30.0 * kPi)) * 2.0 / 3.0;
    return ret;
}

}  // namespace

bool IsInChina(double latitude, double longitude)
{
    return latitude >= 0.8293 && latitude <= 55.8271 && longitude >= 72.004 && longitude <= 137.8347;
}

LatLon Wgs84ToGcj02(double latitude, double longitude)
{
    if (!IsInChina(latitude, longitude)) {
        return {latitude, longitude};
    }
    double dLat = TransformLat(longitude - 105.0, latitude - 35.0);
    double dLon = TransformLon(longitude - 105.0, latitude - 35.0);
    const double radLat = latitude / 180.0 * kPi;
    double magic = std::sin(radLat);
    magic = 1.0 - kEccentricity * magic * magic;
    const double sqrtMagic = std::sqrt(magic);
    dLat = (dLat * 180.0) / ((kEarthAxis * (1.0 - kEccentricity)) / (magic * sqrtMagic) * kPi);
    dLon = (dLon * 180.0) / (kEarthAxis / sqrtMagic * std::cos(radLat) * kPi);
    return {latitude + dLat, longitude + dLon};
}

LatLon Gcj02ToWgs84(double latitude, double longitude)
{
    if (!IsInChina(latitude, longitude)) {
        return {latitude, longitude};
    }
    double wgsLat = latitude;
    double wgsLon = longitude;
    for (int i = 0; i < 6; ++i) {
        const LatLon out = Wgs84ToGcj02(wgsLat, wgsLon);
        wgsLat += latitude - out.latitude;
        wgsLon += longitude - out.longitude;
    }
    return {wgsLat, wgsLon};
}

}  // namespace commute_sa
