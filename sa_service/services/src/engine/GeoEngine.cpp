/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: SA-side offline reverse geocoding via SQLite OSM extract.
 */

#include "GeoEngine.h"

#include <cmath>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "camera_agent_log.h"

// Link against system libsqlite via external_deps (same as other modules).
// Do NOT define SQLITE_CORE here — that mode expects sqlite3.c in the same .so.
#ifndef USE_SQLITE_SYMBOLS
#include <sqlite3.h>
#else
#include "sqlite3sym.h"
#endif 

namespace OHOS::Multimedia::CameraAgentService::GeoEngine {
namespace {

constexpr const char *DEFAULT_DB_PATH =
    "/data/service/el1/public/commuteagentservice/geo/beijing_geocode.db";
constexpr double QUERY_DELTA_DEG = 0.012;
constexpr double MAX_POI_DISTANCE_M = 200.0;
constexpr double MAX_ACCEPT_ROAD_DISTANCE_M = 150.0;
constexpr double ROAD_CANDIDATE_MAX_M = 400.0;
/** In query bbox, snap unnamed geometry (footway/service) to nearest named road. */
constexpr double NAMED_ROAD_SNAP_MAX_M = 120.0;

std::mutex gMutex;
sqlite3 *gDb = nullptr;
bool gInitAttempted = false;

std::mutex gStartMutex;
bool gLatchStartPlace = false;
bool gStartPlaceReady = false;
std::string gStartPlaceJson;

struct RoadCand {
    std::string name;
    std::string highway;
    double lat1 = 0.0;
    double lon1 = 0.0;
    double lat2 = 0.0;
    double lon2 = 0.0;
    double distM = 0.0;
};

struct PoiCand {
    std::string name;
    std::string category;
    double lat = 0.0;
    double lon = 0.0;
    double distM = 0.0;
};

double HaversineM(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double r = 6371000.0;
    const double p1 = lat1 * M_PI / 180.0;
    const double p2 = lat2 * M_PI / 180.0;
    const double dlat = (lat2 - lat1) * M_PI / 180.0;
    const double dlon = (lon2 - lon1) * M_PI / 180.0;
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
        std::cos(p1) * std::cos(p2) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2 * r * std::asin(std::sqrt(a));
}

bool IsInChina(double lat, double lon)
{
    return lat >= 0.8293 && lat <= 55.8271 && lon >= 72.004 && lon <= 137.8347;
}

double TransformLat(double x, double y)
{
    constexpr double pi = 3.14159265358979323846;
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * std::sqrt(std::abs(x));
    ret += (20.0 * std::sin(6.0 * x * pi) + 20.0 * std::sin(2.0 * x * pi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(y * pi) + 40.0 * std::sin(y / 3.0 * pi)) * 2.0 / 3.0;
    ret += (160.0 * std::sin(y / 12.0 * pi) + 320.0 * std::sin(y * pi / 30.0)) * 2.0 / 3.0;
    return ret;
}

double TransformLon(double x, double y)
{
    constexpr double pi = 3.14159265358979323846;
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * std::sqrt(std::abs(x));
    ret += (20.0 * std::sin(6.0 * x * pi) + 20.0 * std::sin(2.0 * x * pi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(x * pi) + 40.0 * std::sin(x / 3.0 * pi)) * 2.0 / 3.0;
    ret += (150.0 * std::sin(x / 12.0 * pi) + 300.0 * std::sin(x / 30.0 * pi)) * 2.0 / 3.0;
    return ret;
}

void Wgs84ToGcj02(double wgsLat, double wgsLon, double &gcjLat, double &gcjLon)
{
    if (!IsInChina(wgsLat, wgsLon)) {
        gcjLat = wgsLat;
        gcjLon = wgsLon;
        return;
    }
    constexpr double pi = 3.14159265358979323846;
    constexpr double earthAxis = 6378245.0;
    constexpr double eccentricity = 0.00669342162296594323;
    double dLat = TransformLat(wgsLon - 105.0, wgsLat - 35.0);
    double dLon = TransformLon(wgsLon - 105.0, wgsLat - 35.0);
    const double radLat = wgsLat / 180.0 * pi;
    double magic = std::sin(radLat);
    magic = 1.0 - eccentricity * magic * magic;
    const double sqrtMagic = std::sqrt(magic);
    dLat = (dLat * 180.0) / ((earthAxis * (1.0 - eccentricity)) / (magic * sqrtMagic) * pi);
    dLon = (dLon * 180.0) / (earthAxis / sqrtMagic * std::cos(radLat) * pi);
    gcjLat = wgsLat + dLat;
    gcjLon = wgsLon + dLon;
}

void Gcj02ToWgs84(double gcjLat, double gcjLon, double &wgsLat, double &wgsLon)
{
    if (!IsInChina(gcjLat, gcjLon)) {
        wgsLat = gcjLat;
        wgsLon = gcjLon;
        return;
    }
    wgsLat = gcjLat;
    wgsLon = gcjLon;
    for (int i = 0; i < 6; ++i) {
        double outLat = 0.0;
        double outLon = 0.0;
        Wgs84ToGcj02(wgsLat, wgsLon, outLat, outLon);
        wgsLat += gcjLat - outLat;
        wgsLon += gcjLon - outLon;
    }
}

std::string RoadDisplayName(const RoadCand &r)
{
    if (!r.name.empty()) {
        return r.name;
    }
    if (!r.highway.empty()) {
        return std::string("道路(") + r.highway + ")";
    }
    return "道路";
}

bool IsNamedRoad(const RoadCand &r)
{
    return !r.name.empty();
}

void PickNearestRoads(
    const std::vector<RoadCand> &roads, RoadCand &bestAny, RoadCand &bestNamed, RoadCand &bestNamedPoiRoad)
{
    bestAny.distM = 1e12;
    bestNamed.distM = 1e12;
    bestNamedPoiRoad.distM = 1e12;
    for (const auto &r : roads) {
        if (r.distM < bestAny.distM) {
            bestAny = r;
        }
        if (!IsNamedRoad(r)) {
            continue;
        }
        if (r.distM < bestNamed.distM) {
            bestNamed = r;
        }
        // Prefer motorable roads for snap when footway and street tie in distance.
        const bool major = r.highway == "primary" || r.highway == "secondary" || r.highway == "tertiary" ||
            r.highway == "trunk" || r.highway == "residential" || r.highway == "living_street" ||
            r.highway == "unclassified" || r.highway == "service";
        if (major && r.distM < bestNamedPoiRoad.distM) {
            bestNamedPoiRoad = r;
        }
    }
    if (bestNamedPoiRoad.distM >= 1e11) {
        bestNamedPoiRoad = bestNamed;
    }
}

struct ResolvedRoad {
    std::string label;
    std::string highway;
    double distM = 1e12;
    std::string matchMode; // direct | snap_named | unnamed_geometry | none
};

ResolvedRoad ResolveRoadLabel(const RoadCand &bestAny, const RoadCand &bestNamed, const RoadCand &bestNamedMajor)
{
    ResolvedRoad out;
    out.matchMode = "none";
    const bool anyOk = bestAny.distM <= MAX_ACCEPT_ROAD_DISTANCE_M;
    const bool namedOk = bestNamed.distM <= NAMED_ROAD_SNAP_MAX_M;
    const bool majorNamedOk = bestNamedMajor.distM <= NAMED_ROAD_SNAP_MAX_M;

    if (anyOk && IsNamedRoad(bestAny)) {
        out.label = bestAny.name;
        out.highway = bestAny.highway;
        out.distM = bestAny.distM;
        out.matchMode = "direct";
        return out;
    }

    const RoadCand *snap = nullptr;
    if (majorNamedOk) {
        snap = &bestNamedMajor;
    } else if (namedOk) {
        snap = &bestNamed;
    }
    if (snap != nullptr) {
        out.label = snap->name;
        out.highway = snap->highway;
        out.distM = snap->distM;
        out.matchMode = "snap_named";
        return out;
    }

    if (anyOk) {
        out.label = RoadDisplayName(bestAny);
        out.highway = bestAny.highway;
        out.distM = bestAny.distM;
        out.matchMode = "unnamed_geometry";
        return out;
    }
    return out;
}

PoiCand PickNearestNamedPoi(const std::vector<PoiCand> &pois, double maxDistM)
{
    PoiCand best;
    best.distM = 1e12;
    for (const auto &p : pois) {
        if (p.name.empty() || p.distM > maxDistM) {
            continue;
        }
        if (p.distM < best.distM) {
            best = p;
        }
    }
    return best;
}

double PointToSegmentDistanceM(double plat, double plon, double lat1, double lon1, double lat2, double lon2)
{
    const double dx = (lon2 - lon1) * std::cos(plat * M_PI / 180.0);
    const double dy = lat2 - lat1;
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) {
        return HaversineM(plat, plon, lat1, lon1);
    }
    const double px = (plon - lon1) * std::cos(plat * M_PI / 180.0);
    const double py = plat - lat1;
    double t = (px * dx + py * dy) / len2;
    if (t < 0.0) {
        t = 0.0;
    } else if (t > 1.0) {
        t = 1.0;
    }
    const double projLat = lat1 + t * dy;
    const double projLon = lon1 + t * dx / std::cos(plat * M_PI / 180.0);
    return HaversineM(plat, plon, projLat, projLon);
}

std::string EscapeJson(const std::string &s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (const char c : s) {
        if (c == '\\' || c == '"') {
            o += '\\';
        }
        if (c == '\n' || c == '\r') {
            continue;
        }
        o += c;
    }
    return o;
}

bool EnsureDbOpen()
{
    if (gDb != nullptr) {
        return true;
    }
    if (gInitAttempted) {
        return false;
    }
    gInitAttempted = true;
    int rc = sqlite3_open_v2(DEFAULT_DB_PATH, &gDb, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || gDb == nullptr) {
        CAMERA_AGENT_LOG_ERROR("GeoEngine sqlite open failed path=%{public}s rc=%{public}d msg=%{public}s",
            DEFAULT_DB_PATH, rc, gDb ? sqlite3_errmsg(gDb) : "null");
        if (gDb != nullptr) {
            sqlite3_close(gDb);
            gDb = nullptr;
        }
        return false;
    }
    CAMERA_AGENT_LOG_INFO("GeoEngine opened %{public}s", DEFAULT_DB_PATH);
    return true;
}

void QueryRoads(double lat, double lon, std::vector<RoadCand> &out)
{
    out.clear();
    if (!EnsureDbOpen()) {
        return;
    }
    const double latMin = lat - QUERY_DELTA_DEG;
    const double latMax = lat + QUERY_DELTA_DEG;
    const double lonMin = lon - QUERY_DELTA_DEG;
    const double lonMax = lon + QUERY_DELTA_DEG;

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT name, highway, lat1, lon1, lat2, lon2 FROM road_segments "
        "WHERE ((lat1 BETWEEN ? AND ? AND lon1 BETWEEN ? AND ?) "
        "OR (lat2 BETWEEN ? AND ? AND lon2 BETWEEN ? AND ?)) LIMIT 800;";
    if (sqlite3_prepare_v2(gDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_double(stmt, 1, latMin);
    sqlite3_bind_double(stmt, 2, latMax);
    sqlite3_bind_double(stmt, 3, lonMin);
    sqlite3_bind_double(stmt, 4, lonMax);
    sqlite3_bind_double(stmt, 5, latMin);
    sqlite3_bind_double(stmt, 6, latMax);
    sqlite3_bind_double(stmt, 7, lonMin);
    sqlite3_bind_double(stmt, 8, lonMax);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RoadCand c;
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        const unsigned char *hw = sqlite3_column_text(stmt, 1);
        c.name = name ? reinterpret_cast<const char *>(name) : "";
        c.highway = hw ? reinterpret_cast<const char *>(hw) : "";
        c.lat1 = sqlite3_column_double(stmt, 2);
        c.lon1 = sqlite3_column_double(stmt, 3);
        c.lat2 = sqlite3_column_double(stmt, 4);
        c.lon2 = sqlite3_column_double(stmt, 5);
        c.distM = PointToSegmentDistanceM(lat, lon, c.lat1, c.lon1, c.lat2, c.lon2);
        if (c.distM <= ROAD_CANDIDATE_MAX_M) {
            out.push_back(c);
        }
    }
    sqlite3_finalize(stmt);
}

void QueryPois(double lat, double lon, std::vector<PoiCand> &out)
{
    out.clear();
    if (!EnsureDbOpen()) {
        return;
    }
    const double latMin = lat - QUERY_DELTA_DEG;
    const double latMax = lat + QUERY_DELTA_DEG;
    const double lonMin = lon - QUERY_DELTA_DEG;
    const double lonMax = lon + QUERY_DELTA_DEG;

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT name, category, lat, lon FROM pois "
        "WHERE lat BETWEEN ? AND ? AND lon BETWEEN ? AND ? LIMIT 300;";
    if (sqlite3_prepare_v2(gDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_double(stmt, 1, latMin);
    sqlite3_bind_double(stmt, 2, latMax);
    sqlite3_bind_double(stmt, 3, lonMin);
    sqlite3_bind_double(stmt, 4, lonMax);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PoiCand c;
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        const unsigned char *cat = sqlite3_column_text(stmt, 1);
        c.name = name ? reinterpret_cast<const char *>(name) : "";
        c.category = cat ? reinterpret_cast<const char *>(cat) : "";
        c.lat = sqlite3_column_double(stmt, 2);
        c.lon = sqlite3_column_double(stmt, 3);
        c.distM = HaversineM(lat, lon, c.lat, c.lon);
        if (!c.name.empty() && c.distM <= MAX_POI_DISTANCE_M) {
            out.push_back(c);
        }
    }
    sqlite3_finalize(stmt);
}

bool IsCommunityCategory(const std::string &cat)
{
    return cat.find("landuse=residential") != std::string::npos ||
        cat.find("building=") != std::string::npos ||
        cat.find("place=") != std::string::npos;
}

} // namespace

bool Init()
{
    std::lock_guard<std::mutex> lock(gMutex);
    return EnsureDbOpen();
}

int GeocodeMatchScore(const std::string &json)
{
    int score = 0;
    if (json.find("\"road\":\"") != std::string::npos && json.find("\"road\":\"\"") == std::string::npos) {
        score += 30;
    }
    if (json.find("\"community\":\"") != std::string::npos && json.find("\"community\":\"\"") == std::string::npos) {
        score += 20;
    }
    if (json.find("\"poi\":\"") != std::string::npos && json.find("\"poi\":\"\"") == std::string::npos) {
        score += 10;
    }
    if (json.find("附近无") == std::string::npos) {
        score += 5;
    }
    return score;
}

std::string ReverseGeocodeAt(double latWgs84, double lonWgs84)
{
    std::vector<RoadCand> roads;
    std::vector<PoiCand> pois;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (!EnsureDbOpen()) {
            return "{\"ready\":false,\"error\":\"db_unavailable\"}";
        }
        QueryRoads(latWgs84, lonWgs84, roads);
        QueryPois(latWgs84, lonWgs84, pois);
    }

    RoadCand bestAny;
    RoadCand bestNamed;
    RoadCand bestNamedMajor;
    PickNearestRoads(roads, bestAny, bestNamed, bestNamedMajor);
    const ResolvedRoad resolved = ResolveRoadLabel(bestAny, bestNamed, bestNamedMajor);
    const bool roadOk = resolved.matchMode != "none" && !resolved.label.empty();

    PoiCand bestPoi;
    bestPoi.distM = 1e12;
    PoiCand bestCommunity;
    bestCommunity.distM = 1e12;
    for (const auto &p : pois) {
        if (p.distM < bestPoi.distM) {
            bestPoi = p;
        }
        if (IsCommunityCategory(p.category) && p.distM < bestCommunity.distM) {
            bestCommunity = p;
        }
    }
    if (bestPoi.name.empty()) {
        bestPoi = PickNearestNamedPoi(pois, MAX_POI_DISTANCE_M);
    }

    std::ostringstream oss;
    oss << "{\"ready\":true";
    oss << ",\"latWgs84\":" << latWgs84 << ",\"lonWgs84\":" << lonWgs84;

    if (roadOk) {
        oss << ",\"road\":\"" << EscapeJson(resolved.label) << "\"";
        oss << ",\"roadDistanceM\":" << static_cast<int>(resolved.distM + 0.5);
        oss << ",\"highway\":\"" << EscapeJson(resolved.highway) << "\"";
        oss << ",\"roadMatch\":\"" << EscapeJson(resolved.matchMode) << "\"";
    } else {
        oss << ",\"road\":\"\"";
        oss << ",\"roadMatch\":\"none\"";
    }

    if (!bestPoi.name.empty()) {
        oss << ",\"poi\":\"" << EscapeJson(bestPoi.name) << "\"";
        oss << ",\"poiCategory\":\"" << EscapeJson(bestPoi.category) << "\"";
        oss << ",\"poiDistanceM\":" << static_cast<int>(bestPoi.distM + 0.5);
    } else {
        oss << ",\"poi\":\"\"";
    }

    if (!bestCommunity.name.empty()) {
        oss << ",\"community\":\"" << EscapeJson(bestCommunity.name) << "\"";
    } else {
        oss << ",\"community\":\"\"";
    }

    std::string summary;
    if (roadOk) {
        summary = resolved.label;
        if (resolved.matchMode == "snap_named") {
            summary += "（附近）";
        }
    }
    if (!bestCommunity.name.empty()) {
        if (!summary.empty()) {
            summary += " · ";
        }
        summary += bestCommunity.name;
    } else if (!bestPoi.name.empty()) {
        if (!summary.empty()) {
            summary += " · ";
        }
        summary += bestPoi.name;
    }
    if (summary.empty()) {
        if (roads.empty() && pois.empty()) {
            summary = "离线库无匹配（请确认在北京范围内且已推送 beijing_geocode.db）";
        } else if (bestAny.distM < 1e11) {
            summary = "附近道路较远(约" + std::to_string(static_cast<int>(bestAny.distM + 0.5)) + "m)，无命名 POI";
        } else {
            summary = "附近无 OSM 要素";
        }
    }
    oss << ",\"summary\":\"" << EscapeJson(summary) << "\"";
    oss << "}";
    return oss.str();
}

std::string ReverseGeocode(double latWgs84, double lonWgs84)
{
    std::string primary = ReverseGeocodeAt(latWgs84, lonWgs84);
    if (primary.find("\"ready\":true") == std::string::npos) {
        return primary;
    }
    if (GeocodeMatchScore(primary) >= 30) {
        return primary;
    }
    double altLat = latWgs84;
    double altLon = lonWgs84;
    Gcj02ToWgs84(latWgs84, lonWgs84, altLat, altLon);
    const double dLat = std::abs(altLat - latWgs84);
    const double dLon = std::abs(altLon - lonWgs84);
    if (dLat < 1e-6 && dLon < 1e-6) {
        return primary;
    }
    std::string alternate = ReverseGeocodeAt(altLat, altLon);
    if (alternate.find("\"ready\":true") == std::string::npos) {
        return primary;
    }
    if (GeocodeMatchScore(alternate) > GeocodeMatchScore(primary)) {
        CAMERA_AGENT_LOG_INFO("GeoEngine geocode used GCJ-corrected WGS lat=%{public}.6f lon=%{public}.6f",
            altLat, altLon);
        return alternate;
    }
    return primary;
}

void ArmStartPlaceLatch()
{
    std::lock_guard<std::mutex> lock(gStartMutex);
    gLatchStartPlace = true;
    gStartPlaceReady = false;
    gStartPlaceJson.clear();
}

void ClearStartPlace()
{
    std::lock_guard<std::mutex> lock(gStartMutex);
    gLatchStartPlace = false;
    gStartPlaceReady = false;
    gStartPlaceJson.clear();
}

std::string GetStartPlaceJson()
{
    std::lock_guard<std::mutex> lock(gStartMutex);
    if (!gStartPlaceReady || gStartPlaceJson.empty()) {
        return "{\"ready\":false,\"hint\":\"等待起点 GPS 或先 StartSensorCollection\"}";
    }
    return gStartPlaceJson;
}

void NotifyLocationSample(double latWgs84, double lonWgs84, double latGcj02, double lonGcj02, double accuracy)
{
    bool shouldLatch = false;
    {
        std::lock_guard<std::mutex> lock(gStartMutex);
        if (!gLatchStartPlace || gStartPlaceReady) {
            return;
        }
        if (accuracy > 0 && accuracy > 100.0) {
            return;
        }
        shouldLatch = true;
    }
    if (!shouldLatch) {
        return;
    }

    std::string place = ReverseGeocode(latWgs84, lonWgs84);
    if (place.find("\"ready\":true") == std::string::npos) {
        return;
    }

    std::ostringstream oss;
    oss << place.substr(0, place.size() - 1);
    oss << ",\"latGcj02\":" << latGcj02 << ",\"lonGcj02\":" << lonGcj02;
    oss << ",\"accuracy\":" << accuracy << ",\"isStart\":true}";

    {
        std::lock_guard<std::mutex> lock(gStartMutex);
        gStartPlaceJson = oss.str();
        gStartPlaceReady = true;
        gLatchStartPlace = false;
    }
    CAMERA_AGENT_LOG_INFO("GeoEngine start place latched: %{public}s", gStartPlaceJson.c_str());
}

} // namespace OHOS::Multimedia::CameraAgentService::GeoEngine
