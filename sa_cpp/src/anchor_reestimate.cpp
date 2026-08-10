#include "commute_sa/anchor_reestimate.h"

#include "commute_sa/baseline_runtime.h"
#include "commute_sa/geo.h"
#include "commute_sa/product_store.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace commute_sa {
namespace {

std::string Esc(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        if (c == '\n' || c == '\r') {
            out.push_back(' ');
            continue;
        }
        out.push_back(c);
    }
    return out;
}

bool PathIsDir(const std::string &path)
{
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
#if defined(_WIN32)
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

std::vector<std::string> ListSubdirs(const std::string &root)
{
    std::vector<std::string> out;
#if defined(_WIN32)
    const std::string pattern = root + "\\*";
    WIN32_FIND_DATAA fd {};
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            continue;
        }
        const std::string name = fd.cFileName;
        if (name == "." || name == "..") {
            continue;
        }
        out.push_back(root + "/" + name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(root.c_str());
    if (d == nullptr) {
        return out;
    }
    while (dirent *ent = readdir(d)) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        const std::string p = root + "/" + ent->d_name;
        if (PathIsDir(p)) {
            out.push_back(p);
        }
    }
    closedir(d);
#endif
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> ListFilesWithPrefix(const std::string &dir, const char *prefix)
{
    std::vector<std::string> out;
#if defined(_WIN32)
    const std::string pattern = dir + "\\*";
    WIN32_FIND_DATAA fd {};
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        const std::string name = fd.cFileName;
        if (name.rfind(prefix, 0) == 0) {
            out.push_back(dir + "/" + name);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir.c_str());
    if (d == nullptr) {
        return out;
    }
    const size_t n = std::strlen(prefix);
    while (dirent *ent = readdir(d)) {
        if (std::strncmp(ent->d_name, prefix, n) == 0) {
            out.push_back(dir + "/" + ent->d_name);
        }
    }
    closedir(d);
#endif
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> SplitCsv(const std::string &line)
{
    std::vector<std::string> cols;
    std::string cur;
    bool inQ = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            inQ = !inQ;
            continue;
        }
        if (c == ',' && !inQ) {
            cols.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    cols.push_back(cur);
    return cols;
}

int LocalHour(int64_t tMs)
{
    std::time_t sec = static_cast<std::time_t>(tMs / 1000);
    std::tm tmValue {};
#if defined(_WIN32)
    localtime_s(&tmValue, &sec);
#else
    localtime_r(&sec, &tmValue);
#endif
    return tmValue.tm_hour;
}

int LocalMinute(int64_t tMs)
{
    std::time_t sec = static_cast<std::time_t>(tMs / 1000);
    std::tm tmValue {};
#if defined(_WIN32)
    localtime_s(&tmValue, &sec);
#else
    localtime_r(&sec, &tmValue);
#endif
    return tmValue.tm_min;
}

double Median(std::vector<double> xs)
{
    if (xs.empty()) {
        return 0.0;
    }
    std::sort(xs.begin(), xs.end());
    const size_t n = xs.size();
    if (n % 2) {
        return xs[n / 2];
    }
    return 0.5 * (xs[n / 2 - 1] + xs[n / 2]);
}

double Percentile90(std::vector<double> xs)
{
    if (xs.empty()) {
        return 0.0;
    }
    std::sort(xs.begin(), xs.end());
    const size_t k = std::min(xs.size() - 1, static_cast<size_t>(std::llround((xs.size() - 1) * 0.9)));
    return xs[k];
}

std::vector<GpsMsLatLon> StillSegments(const std::vector<GpsMsLatLon> &pts, double maxSpeedMps)
{
    if (pts.empty()) {
        return {};
    }
    std::vector<GpsMsLatLon> out;
    out.push_back(pts.front());
    for (size_t i = 1; i < pts.size(); ++i) {
        const auto &a = pts[i - 1];
        const auto &b = pts[i];
        const double dt = std::max(0.5, static_cast<double>(b.first - a.first) / 1000.0);
        const double speed =
            HaversineM(a.second.first, a.second.second, b.second.first, b.second.second) / dt;
        if (speed <= maxSpeedMps) {
            out.push_back(b);
        }
    }
    return out;
}

bool ClusterVote(const std::vector<GpsMsLatLon> &pts, double gridM, double *latOut, double *lonOut, int *nOut,
    std::vector<GpsMsLatLon> *membersOut)
{
    if (pts.size() < 3) {
        return false;
    }
    std::vector<double> lats;
    std::vector<double> lons;
    for (const auto &p : pts) {
        lats.push_back(p.second.first);
        lons.push_back(p.second.second);
    }
    const double lat0 = Median(lats);
    const double lon0 = Median(lons);
    const double mLat = 111320.0;
    const double mLon = 111320.0 * std::cos(lat0 * 3.14159265358979323846 / 180.0);
    std::map<std::pair<int, int>, std::vector<GpsMsLatLon>> buckets;
    for (const auto &p : pts) {
        const int ix = static_cast<int>(std::llround((p.second.first - lat0) * mLat / gridM));
        const int iy = static_cast<int>(std::llround((p.second.second - lon0) * mLon / gridM));
        buckets[{ix, iy}].push_back(p);
    }
    auto bestIt = buckets.begin();
    size_t bestN = 0;
    for (auto it = buckets.begin(); it != buckets.end(); ++it) {
        if (it->second.size() > bestN) {
            bestN = it->second.size();
            bestIt = it;
        }
    }
    std::vector<double> mlats;
    std::vector<double> mlons;
    for (const auto &p : bestIt->second) {
        mlats.push_back(p.second.first);
        mlons.push_back(p.second.second);
    }
    *latOut = Median(mlats);
    *lonOut = Median(mlons);
    *nOut = static_cast<int>(bestIt->second.size());
    if (membersOut) {
        *membersOut = bestIt->second;
    }
    return true;
}

void RadiusFromMembers(double lat, double lon, const std::vector<GpsMsLatLon> &members, double *rIn, double *rOut)
{
    std::vector<double> dists;
    for (const auto &p : members) {
        dists.push_back(HaversineM(p.second.first, p.second.second, lat, lon));
    }
    *rIn = std::max(50.0, std::min(120.0, Percentile90(dists)));
    *rOut = *rIn + 40.0;
}

void LoadLocationCsv(const std::string &path, std::vector<GpsMsLatLon> *out)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return;
    }
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) {
            first = false;
            continue;
        }
        const auto cols = SplitCsv(line);
        if (cols.size() < 3) {
            continue;
        }
        try {
            const int64_t t = std::stoll(cols[0]);
            const double lat = std::stod(cols[1]);
            const double lon = std::stod(cols[2]);
            if (!std::isfinite(lat) || !std::isfinite(lon)) {
                continue;
            }
            out->push_back({t, {lat, lon}});
        } catch (...) {
        }
    }
}

void LoadSparseSamples(const std::string &path, std::vector<GpsMsLatLon> *out)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        int64_t t = 0;
        double lat = 0.0;
        double lon = 0.0;
        const auto extractD = [&](const char *k, double *v) {
            const std::string needle = std::string("\"") + k + "\"";
            const size_t pos = line.find(needle);
            if (pos == std::string::npos) {
                return false;
            }
            size_t i = line.find(':', pos + needle.size());
            if (i == std::string::npos) {
                return false;
            }
            ++i;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
                ++i;
            }
            char *end = nullptr;
            *v = std::strtod(line.c_str() + i, &end);
            return end != line.c_str() + i;
        };
        const auto extractI = [&](const char *k, int64_t *v) {
            double d = 0.0;
            if (!extractD(k, &d)) {
                return false;
            }
            *v = static_cast<int64_t>(d);
            return true;
        };
        if (!extractI("t_ms", &t) || !extractD("lat", &lat) || !extractD("lon", &lon)) {
            continue;
        }
        out->push_back({t, {lat, lon}});
    }
}

std::vector<GpsMsLatLon> CollectAllGps(const std::string &root)
{
    std::vector<GpsMsLatLon> out;
    for (const auto &dir : ListSubdirs(root)) {
        for (const auto &fp : ListFilesWithPrefix(dir, "location_data_")) {
            LoadLocationCsv(fp, &out);
        }
    }
    LoadSparseSamples(root + "/leave_window_samples.jsonl", &out);
    std::sort(out.begin(), out.end(), [](const GpsMsLatLon &a, const GpsMsLatLon &b) { return a.first < b.first; });
    return out;
}

bool InferCompany(const std::vector<GpsMsLatLon> &points, Anchor *out, std::string *err)
{
    std::vector<GpsMsLatLon> day;
    for (const auto &p : points) {
        const int h = LocalHour(p.first);
        if (h >= 9 && h < 18) {
            day.push_back(p);
        }
    }
    auto still = StillSegments(day, 1.5);
    const auto &pool = (still.size() >= 30) ? still : day;
    if (pool.size() < 10) {
        if (err) {
            *err = "not enough daytime GPS for company";
        }
        return false;
    }
    double lat = 0.0;
    double lon = 0.0;
    int n = 0;
    std::vector<GpsMsLatLon> members;
    if (!ClusterVote(pool, 35.0, &lat, &lon, &n, &members)) {
        if (err) {
            *err = "company cluster failed";
        }
        return false;
    }
    double rIn = 80.0;
    double rOut = 120.0;
    RadiusFromMembers(lat, lon, members, &rIn, &rOut);
    out->id = "company_001";
    out->lat = lat;
    out->lon = lon;
    out->r_in_m = rIn;
    out->r_out_m = rOut;
    out->method = "daytime_still_cluster_n=" + std::to_string(n);
    return true;
}

bool InferHome(const std::vector<GpsMsLatLon> &points, const Anchor *company, Anchor *out, std::string *err)
{
    auto still = StillSegments(points, 1.2);
    std::vector<GpsMsLatLon> nightish;
    for (const auto &p : still) {
        const int h = LocalHour(p.first);
        const int m = LocalMinute(p.first);
        if (h < 6 || h >= 23 || (h == 19 && m >= 20) || h >= 20) {
            nightish.push_back(p);
        }
    }
    std::map<std::pair<int64_t, int64_t>, int> rounded;
    for (const auto &p : still) {
        const int64_t rk = static_cast<int64_t>(std::llround(p.second.first * 1e5));
        const int64_t rk2 = static_cast<int64_t>(std::llround(p.second.second * 1e5));
        rounded[{rk, rk2}] += 1;
    }
    std::vector<GpsMsLatLon> sticky;
    for (const auto &p : still) {
        const int64_t rk = static_cast<int64_t>(std::llround(p.second.first * 1e5));
        const int64_t rk2 = static_cast<int64_t>(std::llround(p.second.second * 1e5));
        if (rounded[{rk, rk2}] >= 5) {
            sticky.push_back(p);
        }
    }
    std::vector<GpsMsLatLon> pool = nightish;
    pool.insert(pool.end(), sticky.begin(), sticky.end());
    constexpr double kMinSep = 2000.0;
    if (company != nullptr) {
        std::vector<GpsMsLatLon> filtered;
        for (const auto &p : pool) {
            if (HaversineM(p.second.first, p.second.second, company->lat, company->lon) >= kMinSep) {
                filtered.push_back(p);
            }
        }
        pool.swap(filtered);
    }
    if (pool.size() < 8) {
        if (company == nullptr || still.empty()) {
            if (err) {
                *err = "not enough GPS for home";
            }
            return false;
        }
        std::vector<GpsMsLatLon> farPts = still;
        std::sort(farPts.begin(), farPts.end(), [&](const GpsMsLatLon &a, const GpsMsLatLon &b) {
            return HaversineM(a.second.first, a.second.second, company->lat, company->lon) >
                HaversineM(b.second.first, b.second.second, company->lat, company->lon);
        });
        const size_t take = std::max<size_t>(20, farPts.size() / 4);
        pool.assign(farPts.begin(), farPts.begin() + std::min(take, farPts.size()));
    }
    double lat = 0.0;
    double lon = 0.0;
    int n = 0;
    std::vector<GpsMsLatLon> members;
    if (!ClusterVote(pool, 35.0, &lat, &lon, &n, &members)) {
        if (err) {
            *err = "home cluster failed";
        }
        return false;
    }
    if (company != nullptr) {
        const double sep = HaversineM(lat, lon, company->lat, company->lon);
        if (sep < kMinSep) {
            if (err) {
                *err = "home too close to company: " + std::to_string(static_cast<int>(sep)) + "m";
            }
            return false;
        }
    }
    double rIn = 80.0;
    double rOut = 120.0;
    RadiusFromMembers(lat, lon, members, &rIn, &rOut);
    out->id = "home_001";
    out->lat = lat;
    out->lon = lon;
    out->r_in_m = rIn;
    out->r_out_m = rOut;
    out->method = "night_still+sticky_n=" + std::to_string(n);
    return true;
}

}  // namespace

bool InferAnchorsFromGpsPoints(const std::vector<GpsMsLatLon> &points, const std::string &which,
    const AnchorSet &base, AnchorSet *out, std::string *err)
{
    if (out == nullptr) {
        return false;
    }
    *out = base;
    out->coordinate_system = "WGS84";
    std::ostringstream notes;
    notes << "reestimate which=" << which << " gps_n=" << points.size();

    if (which == "company" || which == "both") {
        Anchor co;
        if (!InferCompany(points, &co, err)) {
            return false;
        }
        out->company = co;
    }
    if (which == "home" || which == "both") {
        Anchor home;
        const Anchor *coPtr = &out->company;
        if (!InferHome(points, coPtr, &home, err)) {
            return false;
        }
        out->home = home;
    }
    std::time_t now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
    out->updated_at = buf;
    out->notes = notes.str();
    return true;
}

std::string ProcessQueuedAnchorReestimateJobs(const std::string &rootDir, int maxJobs)
{
    if (rootDir.empty() || maxJobs <= 0) {
        return "{\"ok\":false,\"error\":\"bad args\"}";
    }
    auto &store = ProductStore::GetInstance();
    if (store.RootDir().empty()) {
        store.Init(rootDir);
    }
    const auto gps = CollectAllGps(rootDir);
    AnchorSet base = DefaultAnchors();
    LoadAnchorsFromFile(rootDir + "/anchors.json", &base, nullptr);

    std::ostringstream oss;
    oss << "{\"ok\":true,\"gps_points\":" << gps.size() << ",\"jobs\":[";
    bool first = true;
    int done = 0;
    for (int i = 0; i < maxJobs; ++i) {
        std::string jobId;
        std::string which;
        int64_t tMs = 0;
        if (!store.PopQueuedAnchorJob(&jobId, &which, &tMs)) {
            break;
        }
        if (!first) {
            oss << ",";
        }
        first = false;
        AnchorSet next = base;
        std::string err;
        if (gps.size() < 10) {
            store.AppendAnchorJobStatus(jobId, "failed", "insufficient_gps");
            oss << "{\"job_id\":\"" << Esc(jobId) << "\",\"which\":\"" << Esc(which)
                << "\",\"status\":\"failed\",\"error\":\"insufficient_gps\"}";
            continue;
        }
        if (!InferAnchorsFromGpsPoints(gps, which, base, &next, &err)) {
            store.AppendAnchorJobStatus(jobId, "failed", err);
            oss << "{\"job_id\":\"" << Esc(jobId) << "\",\"which\":\"" << Esc(which)
                << "\",\"status\":\"failed\",\"error\":\"" << Esc(err) << "\"}";
            continue;
        }
        if (!store.SaveAnchors(next)) {
            store.AppendAnchorJobStatus(jobId, "failed", "SaveAnchors failed");
            oss << "{\"job_id\":\"" << Esc(jobId) << "\",\"which\":\"" << Esc(which)
                << "\",\"status\":\"failed\",\"error\":\"SaveAnchors failed\"}";
            continue;
        }
        if (BaselineRuntime::GetInstance().Engine() != nullptr) {
            BaselineRuntime::GetInstance().Engine()->SetAnchors(next);
        }
        base = next;
        store.AppendAnchorJobStatus(jobId, "done", next.notes);
        ++done;
        oss << "{\"job_id\":\"" << Esc(jobId) << "\",\"which\":\"" << Esc(which)
            << "\",\"status\":\"done\",\"home_lat\":" << next.home.lat << ",\"home_lon\":" << next.home.lon
            << ",\"company_lat\":" << next.company.lat << ",\"company_lon\":" << next.company.lon
            << ",\"method_home\":\"" << Esc(next.home.method) << "\",\"method_company\":\""
            << Esc(next.company.method) << "\"}";
    }
    oss << "],\"processed\":" << done << "}";
    return oss.str();
}

}  // namespace commute_sa
