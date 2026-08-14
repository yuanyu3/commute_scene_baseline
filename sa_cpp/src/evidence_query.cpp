#include "commute_sa/evidence_query.h"

#include "commute_sa/anchors.h"
#include "commute_sa/baseline_runtime.h"
#include "commute_sa/geo.h"
#include "commute_sa/product_store.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
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
    out.reserve(s.size());
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

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ExtractInt64(const std::string &json, const char *key, int64_t *out)
{
    if (out == nullptr || key == nullptr) {
        return false;
    }
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    size_t i = json.find(':', pos + needle.size());
    if (i == std::string::npos) {
        return false;
    }
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
        ++i;
    }
    char *end = nullptr;
    const long long v = std::strtoll(json.c_str() + i, &end, 10);
    if (end == json.c_str() + i) {
        return false;
    }
    *out = static_cast<int64_t>(v);
    return true;
}

bool ExtractNumber(const std::string &json, const char *key, double *out)
{
    if (out == nullptr || key == nullptr) {
        return false;
    }
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    size_t i = json.find(':', pos + needle.size());
    if (i == std::string::npos) {
        return false;
    }
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
        ++i;
    }
    if (i < json.size() && json[i] == 'n') {  // null
        return false;
    }
    char *end = nullptr;
    const double v = std::strtod(json.c_str() + i, &end);
    if (end == json.c_str() + i) {
        return false;
    }
    *out = v;
    return true;
}

bool ExtractString(const std::string &json, const char *key, std::string *out)
{
    if (out == nullptr || key == nullptr) {
        return false;
    }
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    size_t i = json.find(':', pos + needle.size());
    if (i == std::string::npos) {
        return false;
    }
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
        ++i;
    }
    if (i >= json.size() || json[i] != '"') {
        return false;
    }
    ++i;
    std::string val;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            val.push_back(json[i + 1]);
            i += 2;
            continue;
        }
        val.push_back(json[i++]);
    }
    *out = val;
    return true;
}

bool PathIsDir(const std::string &path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}

bool PathIsFile(const std::string &path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFREG) != 0;
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
    DIR *dir = opendir(root.c_str());
    if (dir == nullptr) {
        return out;
    }
    while (dirent *ent = readdir(dir)) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        const std::string p = root + "/" + ent->d_name;
        if (PathIsDir(p)) {
            out.push_back(p);
        }
    }
    closedir(dir);
#endif
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> ListFilesWithPrefix(const std::string &dir, const char *prefix)
{
    std::vector<std::string> out;
    if (dir.empty() || prefix == nullptr) {
        return out;
    }
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

/** Prefer newest session dir that has location/wifi dumps; skip product jsonl-only root. */
std::string PickLatestSessionDir(const std::string &root)
{
    auto dirs = ListSubdirs(root);
    for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
        const std::string &d = *it;
        if (d.find("sa_sensor_test") != std::string::npos) {
            continue;
        }
        if (!ListFilesWithPrefix(d, "location_data_").empty() || !ListFilesWithPrefix(d, "wifi_data_").empty() ||
            !ListFilesWithPrefix(d, "cell_data_").empty() || !ListFilesWithPrefix(d, "mag_data_").empty() ||
            !ListFilesWithPrefix(d, "sensor_events").empty() || !ListFilesWithPrefix(d, "pdr_segment_").empty()) {
            return d;
        }
    }
    return "";
}

int64_t ParseLeadingInt64(const std::string &line)
{
    char *end = nullptr;
    const long long v = std::strtoll(line.c_str(), &end, 10);
    if (end == line.c_str()) {
        return -1;
    }
    return static_cast<int64_t>(v);
}

int64_t LatestPushMsFromEpisodes(const std::string &episodesPath)
{
    std::ifstream in(episodesPath);
    if (!in.is_open()) {
        return 0;
    }
    int64_t latest = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("\"type\":\"push\"") == std::string::npos &&
            line.find("\"type\": \"push\"") == std::string::npos) {
            continue;
        }
        int64_t t = 0;
        if (ExtractInt64(line, "t_push_ms", &t) && t > latest) {
            latest = t;
        }
    }
    return latest;
}

}  // namespace

EvidenceQuery &EvidenceQuery::GetInstance()
{
    static EvidenceQuery inst;
    return inst;
}

void EvidenceQuery::SetRootDir(const std::string &rootDir)
{
    root_ = rootDir;
}

const std::string &EvidenceQuery::RootDir() const
{
    return root_;
}

std::string EvidenceQuery::ResolveRoot() const
{
    if (!root_.empty()) {
        return root_;
    }
    const std::string &ps = ProductStore::GetInstance().RootDir();
    if (!ps.empty()) {
        return ps;
    }
    return "/data/service/el1/public/commuteagentservice";
}

std::string EvidenceQuery::ReadTextFile(const std::string &path) const
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return "";
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::string EvidenceQuery::GetThetaJson() const
{
    const std::string root = ResolveRoot();
    const std::string path = root + "/theta.json";
    const std::string body = ReadTextFile(path);
    if (body.empty()) {
        return "{\"ok\":false,\"error\":\"theta.json missing\",\"path\":\"" + Esc(path) + "\"}";
    }
    return "{\"ok\":true,\"path\":\"" + Esc(path) + "\",\"theta\":" + body + "}";
}

std::string EvidenceQuery::GetAnchorsJson() const
{
    const std::string root = ResolveRoot();
    const std::string path = root + "/anchors.json";
    const std::string body = ReadTextFile(path);
    if (body.empty()) {
        return "{\"ok\":false,\"error\":\"anchors.json missing\",\"path\":\"" + Esc(path) + "\"}";
    }
    return "{\"ok\":true,\"path\":\"" + Esc(path) + "\",\"anchors\":" + body + "}";
}

std::string EvidenceQuery::GetErrorStatsJson(const std::string &paramsJson) const
{
    const std::string root = ResolveRoot();
    const std::string path = root + "/leave_episodes.jsonl";
    int64_t sinceMs = 0;
    ExtractInt64(paramsJson, "since_ms", &sinceMs);
    std::string scene = "ALL";
    ExtractString(paramsJson, "scene", &scene);

    std::ifstream in(path);
    int nPush = 0;
    int nFalse = 0;
    int nConfirmed = 0;
    int nUnknown = 0;
    int nMissed = 0;
    int nLeadLate = 0;
    int nLeadEarly = 0;
    int nLeadOk = 0;
    std::vector<double> leads;
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            int64_t tPush = 0;
            ExtractInt64(line, "t_push_ms", &tPush);
            if (sinceMs > 0 && tPush > 0 && tPush < sinceMs) {
                continue;
            }
            // Missed leave labels use t_push_ms=0 — filter by t_label_ms / t_star_ms.
            if (line.find("MISSED_LEAVE") != std::string::npos) {
                int64_t tLabel = 0;
                ExtractInt64(line, "t_label_ms", &tLabel);
                if (tLabel <= 0) {
                    ExtractInt64(line, "t_star_ms", &tLabel);
                }
                if (sinceMs > 0 && tLabel > 0 && tLabel < sinceMs) {
                    continue;
                }
                ++nMissed;
                continue;
            }
            if (line.find("\"type\":\"push\"") != std::string::npos ||
                line.find("\"type\": \"push\"") != std::string::npos) {
                ++nPush;
            }
            if (line.find("FALSE_PUSH") != std::string::npos) {
                ++nFalse;
            } else if (line.find("CONFIRMED_LEAVE") != std::string::npos) {
                ++nConfirmed;
                double lead = 0.0;
                if (ExtractNumber(line, "lead_s", &lead)) {
                    leads.push_back(lead);
                    if (lead < 90.0) {
                        ++nLeadLate;
                    } else if (lead > 240.0) {
                        ++nLeadEarly;
                    } else {
                        ++nLeadOk;
                    }
                }
            } else if (line.find("\"type\":\"label\"") != std::string::npos ||
                line.find("\"type\": \"label\"") != std::string::npos) {
                if (line.find("UNKNOWN") != std::string::npos) {
                    ++nUnknown;
                }
            }
        }
    }

    auto pct = [&](double q) -> std::string {
        if (leads.empty()) {
            return "null";
        }
        std::vector<double> sorted = leads;
        std::sort(sorted.begin(), sorted.end());
        const double idx = q * static_cast<double>(sorted.size() - 1);
        const size_t i = static_cast<size_t>(idx);
        const size_t j = std::min(i + 1, sorted.size() - 1);
        const double frac = idx - static_cast<double>(i);
        const double v = sorted[i] * (1.0 - frac) + sorted[j] * frac;
        std::ostringstream o;
        o << std::setprecision(6) << v;
        return o.str();
    };

    std::ostringstream oss;
    oss << "{\"ok\":true,\"path\":\"" << Esc(path) << "\",\"scene\":\"" << Esc(scene)
        << "\",\"since_ms\":" << sinceMs << ",\"n_push\":" << nPush << ",\"n_false_push\":" << nFalse
        << ",\"n_confirmed_leave\":" << nConfirmed << ",\"n_unknown_label\":" << nUnknown
        << ",\"n_missed_leave\":" << nMissed << ",\"n_lead_samples\":" << leads.size()
        << ",\"n_lead_late\":" << nLeadLate << ",\"n_lead_ok\":" << nLeadOk << ",\"n_lead_early\":" << nLeadEarly
        << ",\"lead_p50_s\":" << pct(0.5) << ",\"lead_p90_s\":" << pct(0.9)
        << ",\"notes\":\"lead_s = t_star_outside - t_push; MISSED_LEAVE = sustained OUTSIDE without prior push\"}";
    return oss.str();
}

std::string EvidenceQuery::GetLeaveEpisodeJson(const std::string &paramsJson) const
{
    const std::string root = ResolveRoot();
    const std::string path = root + "/leave_episodes.jsonl";
    int64_t wantPush = 0;
    const bool hasWant = ExtractInt64(paramsJson, "t_push_ms", &wantPush);
    if (!hasWant || wantPush <= 0) {
        wantPush = LatestPushMsFromEpisodes(path);
    }
    if (wantPush <= 0) {
        return "{\"ok\":false,\"error\":\"no push episode found\"}";
    }

    std::ifstream in(path);
    std::vector<std::string> matched;
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            int64_t t = 0;
            if (!ExtractInt64(line, "t_push_ms", &t) || t != wantPush) {
                continue;
            }
            matched.push_back(line);
        }
    }
    std::ostringstream oss;
    oss << "{\"ok\":true,\"t_push_ms\":" << wantPush << ",\"n\":" << matched.size() << ",\"rows\":[";
    for (size_t i = 0; i < matched.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << matched[i];
    }
    oss << "]}";
    return oss.str();
}

std::string EvidenceQuery::GetLeaveWindowSamplesJson(const std::string &paramsJson) const
{
    const std::string root = ResolveRoot();
    const std::string path = root + "/leave_window_samples.jsonl";
    int64_t wantPush = 0;
    ExtractInt64(paramsJson, "t_push_ms", &wantPush);
    if (wantPush <= 0) {
        wantPush = LatestPushMsFromEpisodes(root + "/leave_episodes.jsonl");
    }
    int64_t limit = 100;
    ExtractInt64(paramsJson, "limit", &limit);
    if (limit <= 0) {
        limit = 100;
    }
    if (limit > 500) {
        limit = 500;
    }

    std::ifstream in(path);
    std::vector<std::string> rows;
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            if (wantPush > 0) {
                int64_t t = 0;
                if (!ExtractInt64(line, "t_push_ms", &t) || t != wantPush) {
                    continue;
                }
            }
            rows.push_back(line);
            if (static_cast<int64_t>(rows.size()) >= limit) {
                break;
            }
        }
    }
    std::ostringstream oss;
    oss << "{\"ok\":true,\"t_push_ms\":" << wantPush << ",\"n\":" << rows.size() << ",\"samples\":[";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << rows[i];
    }
    oss << "]}";
    return oss.str();
}

std::string EvidenceQuery::SensorWindowJson(const std::string &paramsJson, const char *filePrefix,
    const char *sensorName) const
{
    const std::string root = ResolveRoot();
    int64_t tCenter = 0;
    ExtractInt64(paramsJson, "t_center_ms", &tCenter);
    if (tCenter <= 0) {
        ExtractInt64(paramsJson, "t_push_ms", &tCenter);
    }
    if (tCenter <= 0) {
        tCenter = LatestPushMsFromEpisodes(root + "/leave_episodes.jsonl");
    }
    int64_t beforeS = 600;
    int64_t afterS = 1200;
    ExtractInt64(paramsJson, "before_s", &beforeS);
    ExtractInt64(paramsJson, "after_s", &afterS);
    if (beforeS < 0) {
        beforeS = 0;
    }
    if (afterS < 0) {
        afterS = 0;
    }
    int64_t limit = 200;
    ExtractInt64(paramsJson, "limit", &limit);
    if (limit <= 0) {
        limit = 200;
    }
    if (limit > 500) {
        limit = 500;
    }

    std::string sessionDir;
    ExtractString(paramsJson, "session_dir", &sessionDir);
    if (sessionDir.empty()) {
        sessionDir = PickLatestSessionDir(root);
    }
    if (sessionDir.empty() || !PathIsDir(sessionDir)) {
        std::ostringstream oss;
        oss << "{\"ok\":false,\"sensor\":\"" << sensorName << "\",\"error\":\"no Ability session dump dir under "
            << Esc(root) << "\",\"hint\":\"start collection so wifi/cell/mag/location CSVs exist\"}";
        return oss.str();
    }

    const auto files = ListFilesWithPrefix(sessionDir, filePrefix);
    if (files.empty()) {
        return std::string("{\"ok\":false,\"sensor\":\"") + sensorName +
            "\",\"session_dir\":\"" + Esc(sessionDir) + "\",\"error\":\"no files with prefix " + filePrefix + "\"}";
    }

    const int64_t t0 = tCenter > 0 ? (tCenter - beforeS * 1000) : 0;
    const int64_t t1 = tCenter > 0 ? (tCenter + afterS * 1000) : INT64_MAX;

    // Split budget across pre/post center so dense pre-push wifi cannot starve post-push rows.
    // Pre: keep rows closest to t_center (ring buffer). Post: keep earliest after center.
    const int64_t beforeBudget = (tCenter > 0) ? (limit / 2) : limit;
    const int64_t afterBudget = (tCenter > 0) ? (limit - beforeBudget) : 0;

    std::deque<std::string> beforeLines;
    std::vector<std::string> afterLines;
    std::string header;
    bool afterFull = afterBudget <= 0;
    for (const auto &fp : files) {
        std::ifstream in(fp);
        if (!in.is_open()) {
            continue;
        }
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first) {
                header = line;
                first = false;
                continue;
            }
            if (line.empty()) {
                continue;
            }
            const int64_t ts = ParseLeadingInt64(line);
            if (ts < 0) {
                continue;
            }
            if (tCenter > 0) {
                if (ts < t0) {
                    continue;
                }
                if (ts > t1) {
                    // files are time-prefixed; later rows in this file / later files are later
                    if (afterFull) {
                        break;
                    }
                    continue;
                }
                if (ts < tCenter) {
                    beforeLines.push_back(line);
                    if (static_cast<int64_t>(beforeLines.size()) > beforeBudget) {
                        beforeLines.pop_front();
                    }
                } else if (!afterFull) {
                    afterLines.push_back(line);
                    if (static_cast<int64_t>(afterLines.size()) >= afterBudget) {
                        afterFull = true;
                    }
                }
            } else {
                beforeLines.push_back(line);
                if (static_cast<int64_t>(beforeLines.size()) >= limit) {
                    afterFull = true;
                    break;
                }
            }
        }
        if (tCenter <= 0 && static_cast<int64_t>(beforeLines.size()) >= limit) {
            break;
        }
        // If after is full and this file's name hour is clearly past window, still OK to continue
        // other files that might overlap; only stop when afterFull and we already passed t1 in-file.
        if (afterFull && tCenter > 0) {
            // keep scanning for any remaining pre-center rows in later-listed files? usually files
            // are chronological so pre-center is done. Safe to break once afterFull.
            // But a file can contain both pre and post; we only break inner loop on ts>t1.
            // Across files: if afterFull, still need pre-center from earlier times — those are in
            // earlier files already processed. So we can break outer loop when afterFull.
            break;
        }
    }

    std::vector<std::string> lines;
    lines.reserve(beforeLines.size() + afterLines.size());
    const size_t nBefore = beforeLines.size();
    const size_t nAfter = afterLines.size();
    for (auto &s : beforeLines) {
        lines.push_back(std::move(s));
    }
    for (auto &s : afterLines) {
        lines.push_back(std::move(s));
    }

    std::ostringstream oss;
    oss << "{\"ok\":true,\"sensor\":\"" << sensorName << "\",\"session_dir\":\"" << Esc(sessionDir)
        << "\",\"t_center_ms\":" << tCenter << ",\"before_s\":" << beforeS << ",\"after_s\":" << afterS
        << ",\"limit\":" << limit << ",\"n_before\":" << nBefore << ",\"n_after\":" << nAfter
        << ",\"header\":\"" << Esc(header) << "\",\"n\":" << lines.size() << ",\"rows\":[";
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << "\"" << Esc(lines[i]) << "\"";
    }
    oss << "]}";
    return oss.str();
}

namespace {

std::vector<std::string> SplitCsvFields(const std::string &line)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

double JaccardSets(const std::unordered_set<std::string> &a, const std::unordered_set<std::string> &b)
{
    if (a.empty() && b.empty()) {
        return 1.0;
    }
    if (a.empty() || b.empty()) {
        return 0.0;
    }
    size_t inter = 0;
    for (const auto &x : a) {
        if (b.count(x) != 0) {
            ++inter;
        }
    }
    const size_t uni = a.size() + b.size() - inter;
    return uni == 0 ? 1.0 : static_cast<double>(inter) / static_cast<double>(uni);
}

std::unordered_set<std::string> ExtractSoftBssids(const std::string &softJson, const char *side)
{
    std::unordered_set<std::string> out;
    const std::string key = std::string("\"") + side + "\"";
    const auto pos = softJson.find(key);
    if (pos == std::string::npos) {
        return out;
    }
    const auto bpos = softJson.find("\"bssids\"", pos);
    if (bpos == std::string::npos || bpos > pos + 400) {
        return out;
    }
    const auto lb = softJson.find('[', bpos);
    const auto rb = softJson.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos) {
        return out;
    }
    const std::string arr = softJson.substr(lb + 1, rb - lb - 1);
    size_t i = 0;
    while (i < arr.size()) {
        const auto q1 = arr.find('"', i);
        if (q1 == std::string::npos) {
            break;
        }
        const auto q2 = arr.find('"', q1 + 1);
        if (q2 == std::string::npos) {
            break;
        }
        out.insert(arr.substr(q1 + 1, q2 - q1 - 1));
        i = q2 + 1;
    }
    return out;
}

struct WifiScanBucket {
    int64_t t_ms = 0;
    std::unordered_map<std::string, int32_t> rssi;  // bssid -> best rssi
};

void LoadWifiBuckets(const std::string &sessionDir, int64_t t0, int64_t t1, std::map<int64_t, WifiScanBucket> *out)
{
    if (out == nullptr) {
        return;
    }
    for (const auto &fp : ListFilesWithPrefix(sessionDir, "wifi_data_")) {
        std::ifstream in(fp);
        if (!in.is_open()) {
            continue;
        }
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first) {
                first = false;
                continue;
            }
            const auto cols = SplitCsvFields(line);
            if (cols.size() < 4) {
                continue;
            }
            const int64_t ts = ParseLeadingInt64(cols[0]);
            if (ts < t0 || ts > t1) {
                continue;
            }
            const std::string &bssid = cols[1];
            if (bssid.empty()) {
                continue;
            }
            int32_t rssi = -127;
            try {
                rssi = static_cast<int32_t>(std::stoi(cols[3]));
            } catch (...) {
                continue;
            }
            auto &b = (*out)[ts];
            b.t_ms = ts;
            auto it = b.rssi.find(bssid);
            if (it == b.rssi.end() || rssi > it->second) {
                b.rssi[bssid] = rssi;
            }
        }
    }
}

std::unordered_set<std::string> StrongFromBucket(const WifiScanBucket &b, int32_t rssiMin)
{
    std::unordered_set<std::string> s;
    for (const auto &kv : b.rssi) {
        if (kv.second >= rssiMin) {
            s.insert(kv.first);
        }
    }
    return s;
}

const WifiScanBucket *NearestBucket(const std::map<int64_t, WifiScanBucket> &m, int64_t t, int64_t maxDelta)
{
    if (m.empty()) {
        return nullptr;
    }
    auto it = m.lower_bound(t);
    const WifiScanBucket *best = nullptr;
    int64_t bestD = maxDelta + 1;
    if (it != m.end()) {
        best = &it->second;
        bestD = std::llabs(it->first - t);
    }
    if (it != m.begin()) {
        --it;
        const int64_t d = std::llabs(it->first - t);
        if (d < bestD) {
            best = &it->second;
            bestD = d;
        }
    }
    return (best != nullptr && bestD <= maxDelta) ? best : nullptr;
}

struct CellPoint {
    int64_t t_ms = 0;
    int64_t cell_id = 0;
    int32_t rssi = -127;
};

void LoadCells(const std::string &sessionDir, int64_t t0, int64_t t1, std::vector<CellPoint> *out)
{
    if (out == nullptr) {
        return;
    }
    for (const auto &fp : ListFilesWithPrefix(sessionDir, "cell_data_")) {
        std::ifstream in(fp);
        if (!in.is_open()) {
            continue;
        }
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first) {
                first = false;
                continue;
            }
            const auto cols = SplitCsvFields(line);
            if (cols.size() < 4) {
                continue;
            }
            const int64_t ts = ParseLeadingInt64(cols[0]);
            if (ts < t0 || ts > t1) {
                continue;
            }
            CellPoint p;
            p.t_ms = ts;
            try {
                p.cell_id = std::stoll(cols[2]);
                p.rssi = static_cast<int32_t>(std::stoi(cols[3]));
            } catch (...) {
                continue;
            }
            if (p.cell_id != 0) {
                out->push_back(p);
            }
        }
    }
    std::sort(out->begin(), out->end(), [](const CellPoint &a, const CellPoint &b) { return a.t_ms < b.t_ms; });
}

struct GpsPointLite {
    int64_t t_ms = 0;
    double lat = 0.0;
    double lon = 0.0;
    double acc = 0.0;
};

void LoadGps(const std::string &sessionDir, int64_t t0, int64_t t1, std::vector<GpsPointLite> *out)
{
    if (out == nullptr) {
        return;
    }
    for (const auto &fp : ListFilesWithPrefix(sessionDir, "location_data_")) {
        std::ifstream in(fp);
        if (!in.is_open()) {
            continue;
        }
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first) {
                first = false;
                continue;
            }
            const auto cols = SplitCsvFields(line);
            if (cols.size() < 4) {
                continue;
            }
            const int64_t ts = ParseLeadingInt64(cols[0]);
            if (ts < t0 || ts > t1) {
                continue;
            }
            GpsPointLite p;
            p.t_ms = ts;
            try {
                p.lat = std::stod(cols[1]);
                p.lon = std::stod(cols[2]);
                p.acc = std::stod(cols[3]);
            } catch (...) {
                continue;
            }
            out->push_back(p);
        }
    }
    std::sort(out->begin(), out->end(), [](const GpsPointLite &a, const GpsPointLite &b) { return a.t_ms < b.t_ms; });
}

/** Parse ISO-8601 with optional millis and +HH:MM / Z → epoch ms. */
int64_t ParseIso8601ToMs(const std::string &s)
{
    int y = 0;
    int mo = 0;
    int d = 0;
    int h = 0;
    int mi = 0;
    int sec = 0;
    int ms = 0;
    char sign = '+';
    int offH = 0;
    int offM = 0;
    bool hasOff = false;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d.%d%c%d:%d", &y, &mo, &d, &h, &mi, &sec, &ms, &sign, &offH,
            &offM) >= 10) {
        hasOff = (sign == '+' || sign == '-');
    } else if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d%c%d:%d", &y, &mo, &d, &h, &mi, &sec, &sign, &offH, &offM) >=
        9) {
        ms = 0;
        hasOff = (sign == '+' || sign == '-');
    } else if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d.%dZ", &y, &mo, &d, &h, &mi, &sec, &ms) >= 7) {
        hasOff = true;
        sign = '+';
        offH = offM = 0;
    } else if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%dZ", &y, &mo, &d, &h, &mi, &sec) >= 6) {
        ms = 0;
        hasOff = true;
        sign = '+';
        offH = offM = 0;
    } else {
        return -1;
    }
    std::tm tmValue {};
    tmValue.tm_year = y - 1900;
    tmValue.tm_mon = mo - 1;
    tmValue.tm_mday = d;
    tmValue.tm_hour = h;
    tmValue.tm_min = mi;
    tmValue.tm_sec = sec;
#if defined(_WIN32)
    const time_t asUtc = _mkgmtime(&tmValue);
#else
    const time_t asUtc = timegm(&tmValue);
#endif
    if (asUtc < 0) {
        return -1;
    }
    int64_t epoch = static_cast<int64_t>(asUtc) * 1000 + ms;
    if (hasOff) {
        int offSec = offH * 3600 + offM * 60;
        if (sign == '-') {
            offSec = -offSec;
        }
        epoch -= static_cast<int64_t>(offSec) * 1000;
    }
    return epoch;
}

struct PdrSeriesPoint {
    int64_t t_ms = 0;
    double net_m = 0.0;
    double path_m = 0.0;
    double straightness = 0.0;
};

struct PdrSummary {
    int episode_count = 0;
    int point_count = 0;
    double max_net_m = 0.0;
    double max_path_m = 0.0;
    double net_at_push = 0.0;
    double path_at_push = 0.0;
    double straightness_at_push = 0.0;
    double net_pre = 0.0;
    double net_post = 0.0;
    bool available = false;
};

void LoadPdrFromSensorEvents(const std::string &sessionDir, int64_t t0, int64_t t1, int64_t tCenter,
    PdrSummary *sum)
{
    if (sum == nullptr) {
        return;
    }
    const std::string csvPath = sessionDir + "/sensor_events.csv";
    std::ifstream in(csvPath);
    if (!in.is_open()) {
        return;
    }
    std::string line;
    bool first = true;
    bool hasOrigin = false;
    double ox = 0.0;
    double oy = 0.0;
    double lx = 0.0;
    double ly = 0.0;
    double pathLen = 0.0;
    int epPts = 0;
    std::string episode;
    std::vector<PdrSeriesPoint> series;
    while (std::getline(in, line)) {
        if (first) {
            first = false;
            continue;
        }
        const auto cols = SplitCsvFields(line);
        if (cols.size() < 7) {
            continue;
        }
        const std::string &etype = cols[3];
        const int64_t t = ParseIso8601ToMs(cols[2]);
        if (t < 0) {
            continue;
        }
        if (etype == "WALKING_STARTED") {
            hasOrigin = false;
            pathLen = 0.0;
            epPts = 0;
            episode = cols[5];
            ++sum->episode_count;
            continue;
        }
        if (etype == "WALKING_STOPPED") {
            hasOrigin = false;
            continue;
        }
        if (etype != "PDR_POINT") {
            continue;
        }
        if (t < t0 || t > t1) {
            continue;
        }
        double x = 0.0;
        double y = 0.0;
        const std::string &payload = cols[6];
        ExtractNumber(payload, "x", &x);
        ExtractNumber(payload, "y", &y);
        if (!hasOrigin) {
            ox = x;
            oy = y;
            lx = x;
            ly = y;
            hasOrigin = true;
            pathLen = 0.0;
            epPts = 1;
        } else {
            pathLen += std::hypot(x - lx, y - ly);
            lx = x;
            ly = y;
            ++epPts;
        }
        const double net = std::hypot(x - ox, y - oy);
        PdrSeriesPoint p;
        p.t_ms = t;
        p.net_m = net;
        p.path_m = pathLen;
        p.straightness = (pathLen > 1e-6) ? std::min(1.0, net / pathLen) : 0.0;
        series.push_back(p);
        sum->max_net_m = std::max(sum->max_net_m, net);
        sum->max_path_m = std::max(sum->max_path_m, pathLen);
        ++sum->point_count;
        (void)episode;
    }
    if (series.empty()) {
        return;
    }
    sum->available = true;
    auto nearest = [&](int64_t t) -> PdrSeriesPoint {
        PdrSeriesPoint best = series.front();
        int64_t bestD = std::llabs(best.t_ms - t);
        for (const auto &p : series) {
            const int64_t d = std::llabs(p.t_ms - t);
            if (d < bestD) {
                bestD = d;
                best = p;
            }
        }
        return best;
    };
    const auto pre = nearest(tCenter - 180000);
    const auto at = nearest(tCenter);
    const auto post = nearest(tCenter + 300000);
    sum->net_pre = pre.net_m;
    sum->net_at_push = at.net_m;
    sum->path_at_push = at.path_m;
    sum->straightness_at_push = at.straightness;
    sum->net_post = post.net_m;
}

void LoadPdrFromSegments(const std::string &sessionDir, int64_t t0, int64_t t1, int64_t tCenter, PdrSummary *sum)
{
    if (sum == nullptr || sum->available) {
        return;
    }
    for (const auto &fp : ListFilesWithPrefix(sessionDir, "pdr_segment_")) {
        std::ifstream in(fp);
        if (!in.is_open()) {
            continue;
        }
        std::string line;
        bool first = true;
        bool hasOrigin = false;
        double ox = 0.0;
        double oy = 0.0;
        double pathLen = 0.0;
        double lx = 0.0;
        double ly = 0.0;
        std::vector<PdrSeriesPoint> series;
        while (std::getline(in, line)) {
            if (first) {
                first = false;
                continue;
            }
            const auto cols = SplitCsvFields(line);
            if (cols.size() < 3) {
                continue;
            }
            int64_t t = ParseLeadingInt64(cols[0]);
            if (t < 0) {
                continue;
            }
            if (t < t0 || t > t1) {
                continue;
            }
            double x = 0.0;
            double y = 0.0;
            try {
                x = std::stod(cols[1]);
                y = std::stod(cols[2]);
            } catch (...) {
                continue;
            }
            if (!hasOrigin) {
                ox = x;
                oy = y;
                lx = x;
                ly = y;
                hasOrigin = true;
                pathLen = 0.0;
            } else {
                pathLen += std::hypot(x - lx, y - ly);
                lx = x;
                ly = y;
            }
            const double net = std::hypot(x - ox, y - oy);
            PdrSeriesPoint p;
            p.t_ms = t;
            p.net_m = net;
            p.path_m = pathLen;
            p.straightness = (pathLen > 1e-6) ? std::min(1.0, net / pathLen) : 0.0;
            series.push_back(p);
            sum->max_net_m = std::max(sum->max_net_m, net);
            sum->max_path_m = std::max(sum->max_path_m, pathLen);
            ++sum->point_count;
        }
        if (!series.empty()) {
            ++sum->episode_count;
            sum->available = true;
            auto nearest = [&](int64_t t) -> PdrSeriesPoint {
                PdrSeriesPoint best = series.front();
                int64_t bestD = std::llabs(best.t_ms - t);
                for (const auto &p : series) {
                    const int64_t d = std::llabs(p.t_ms - t);
                    if (d < bestD) {
                        bestD = d;
                        best = p;
                    }
                }
                return best;
            };
            sum->net_pre = nearest(tCenter - 180000).net_m;
            const auto at = nearest(tCenter);
            sum->net_at_push = at.net_m;
            sum->path_at_push = at.path_m;
            sum->straightness_at_push = at.straightness;
            sum->net_post = nearest(tCenter + 300000).net_m;
        }
    }
}

}  // namespace

std::string EvidenceQuery::GetLeaveSensorSummaryJson(const std::string &paramsJson) const
{
    const std::string root = ResolveRoot();
    int64_t tCenter = 0;
    ExtractInt64(paramsJson, "t_center_ms", &tCenter);
    if (tCenter <= 0) {
        ExtractInt64(paramsJson, "t_push_ms", &tCenter);
    }
    if (tCenter <= 0) {
        tCenter = LatestPushMsFromEpisodes(root + "/leave_episodes.jsonl");
    }
    if (tCenter <= 0) {
        return "{\"ok\":false,\"error\":\"no t_push_ms / t_center_ms\"}";
    }
    int64_t beforeS = 600;
    int64_t afterS = 1200;
    ExtractInt64(paramsJson, "before_s", &beforeS);
    ExtractInt64(paramsJson, "after_s", &afterS);
    if (beforeS < 60) {
        beforeS = 60;
    }
    if (afterS < 60) {
        afterS = 60;
    }

    std::string sessionDir;
    ExtractString(paramsJson, "session_dir", &sessionDir);
    if (sessionDir.empty()) {
        sessionDir = PickLatestSessionDir(root);
    }

    const int64_t t0 = tCenter - beforeS * 1000;
    const int64_t t1 = tCenter + afterS * 1000;

    std::string softJson;
    ProductStore::GetInstance().LoadRadioSoftJson(&softJson);
    auto softCompany = ExtractSoftBssids(softJson, "company");
    auto softHome = ExtractSoftBssids(softJson, "home");
    const auto &soft =
        softCompany.size() >= softHome.size() ? softCompany : softHome;
    const char *softSide = softCompany.size() >= softHome.size() ? "company" : "home";

    // Cross-session company profile (same source SceneEngine uses when present).
    // Prefer it over session dwell soft so detach_hint aligns with obs_wifi_detach.
    const std::string fpJson = ReadTextFile(root + "/company_radio_fingerprint.json");
    std::unordered_set<std::string> fingerprint;
    for (const auto &b : ExtractSoftBssids(fpJson, "company")) {
        fingerprint.insert(Lower(b));
    }
    const bool useSiteFp = fingerprint.size() >= 2;
    const auto &wifiRef = useSiteFp ? fingerprint : soft;
    const char *wifiRefSide = useSiteFp ? "company" : softSide;
    const char *wifiRefSource = useSiteFp ? "company_fingerprint" : "radio_soft";

    std::map<int64_t, WifiScanBucket> wifi;
    std::vector<CellPoint> cells;
    std::vector<GpsPointLite> gps;
    if (!sessionDir.empty() && PathIsDir(sessionDir)) {
        LoadWifiBuckets(sessionDir, t0, t1, &wifi);
        LoadCells(sessionDir, t0, t1, &cells);
        LoadGps(sessionDir, t0, t1, &gps);
    }

    AnchorSet anchors = DefaultAnchors();
    LoadAnchorsFromFile(root + "/anchors.json", &anchors, nullptr);

    auto siteDetach = [&](const std::unordered_set<std::string> &strong, int *matchesOut,
                          double *coverageOut) -> bool {
        int matches = 0;
        for (const auto &bssid : strong) {
            if (fingerprint.count(Lower(bssid)) != 0) {
                ++matches;
            }
        }
        const double coverage =
            strong.empty() ? 0.0 : static_cast<double>(matches) / static_cast<double>(strong.size());
        if (matchesOut != nullptr) {
            *matchesOut = matches;
        }
        if (coverageOut != nullptr) {
            *coverageOut = coverage;
        }
        // Mirrors python RadioEvidence._detach_site_wifi / C++ site path.
        return matches < 2 || coverage < 0.35;
    };

    auto wifiSnap = [&](int64_t t, const char *label) -> std::string {
        const WifiScanBucket *b = NearestBucket(wifi, t, 90000);
        std::ostringstream o;
        o << "{\"label\":\"" << label << "\",\"t_target_ms\":" << t;
        if (b == nullptr) {
            o << ",\"available\":false}";
            return o.str();
        }
        const auto strong = StrongFromBucket(*b, -85);
        o << ",\"available\":true,\"t_ms\":" << b->t_ms << ",\"n_strong\":" << strong.size()
          << ",\"ref_source\":\"" << wifiRefSource << "\"";
        if (useSiteFp) {
            int matches = 0;
            double coverage = 0.0;
            const bool detach = siteDetach(strong, &matches, &coverage);
            o << ",\"site_matches\":" << matches << ",\"site_coverage\":" << coverage
              << ",\"jaccard_to_soft\":-1,\"soft_missing_or_weak\":0,\"soft_checked\":0"
              << ",\"detach_hint\":" << (detach ? "true" : "false") << "}";
            return o.str();
        }
        const double jac = wifiRef.empty() ? -1.0 : JaccardSets(strong, wifiRef);
        int overlapDrop = 0;
        int overlapN = 0;
        for (const auto &bssid : wifiRef) {
            auto it = b->rssi.find(bssid);
            if (it == b->rssi.end()) {
                ++overlapDrop;
                ++overlapN;
            } else {
                ++overlapN;
                if (it->second < -80) {
                    ++overlapDrop;
                }
            }
        }
        o << ",\"jaccard_to_soft\":" << (jac < 0 ? -1.0 : jac) << ",\"soft_missing_or_weak\":" << overlapDrop
          << ",\"soft_checked\":" << overlapN << ",\"detach_hint\":"
          << ((jac >= 0 && jac < 0.30) || (overlapN >= 2 && overlapDrop * 2 >= overlapN) ? "true" : "false") << "}";
        return o.str();
    };

    // WiFi events: site detach rising-edge, or soft jaccard cross below 0.3
    std::ostringstream wifiEvents;
    wifiEvents << "[";
    bool firstEv = true;
    double prevJac = 1.0;
    bool prevSiteDetach = false;
    bool havePrev = false;
    int scanN = 0;
    for (const auto &kv : wifi) {
        ++scanN;
        if (wifiRef.empty()) {
            continue;
        }
        const auto strong = StrongFromBucket(kv.second, -85);
        if (useSiteFp) {
            int matches = 0;
            double coverage = 0.0;
            const bool detach = siteDetach(strong, &matches, &coverage);
            if (havePrev && !prevSiteDetach && detach) {
                if (!firstEv) {
                    wifiEvents << ",";
                }
                firstEv = false;
                wifiEvents << "{\"t_ms\":" << kv.first << ",\"type\":\"site_detach_on\",\"site_matches\":" << matches
                           << ",\"site_coverage\":" << coverage << "}";
            }
            prevSiteDetach = detach;
            havePrev = true;
            continue;
        }
        const double jac = JaccardSets(strong, wifiRef);
        if (havePrev && prevJac >= 0.30 && jac < 0.30) {
            if (!firstEv) {
                wifiEvents << ",";
            }
            firstEv = false;
            wifiEvents << "{\"t_ms\":" << kv.first << ",\"type\":\"jaccard_cross_below_0.3\",\"from\":" << prevJac
                       << ",\"to\":" << jac << "}";
        }
        prevJac = jac;
        havePrev = true;
    }
    wifiEvents << "]";
    // Cell: dominant id pre/at/post + changes
    auto cellDom = [&](int64_t lo, int64_t hi) -> std::pair<int64_t, int> {
        std::unordered_map<int64_t, int> cnt;
        for (const auto &c : cells) {
            if (c.t_ms >= lo && c.t_ms <= hi) {
                cnt[c.cell_id] += 1;
            }
        }
        int64_t best = 0;
        int bestN = 0;
        for (const auto &kv : cnt) {
            if (kv.second > bestN) {
                best = kv.first;
                bestN = kv.second;
            }
        }
        return {best, bestN};
    };
    const auto preC = cellDom(tCenter - 180000, tCenter);
    const auto atC = cellDom(tCenter - 30000, tCenter + 30000);
    const auto postC = cellDom(tCenter, tCenter + 300000);
    std::ostringstream cellChanges;
    cellChanges << "[";
    bool firstCc = true;
    int64_t lastId = 0;
    for (const auto &c : cells) {
        if (lastId != 0 && c.cell_id != lastId) {
            if (!firstCc) {
                cellChanges << ",";
            }
            firstCc = false;
            cellChanges << "{\"t_ms\":" << c.t_ms << ",\"from\":" << lastId << ",\"to\":" << c.cell_id << "}";
        }
        lastId = c.cell_id;
    }
    cellChanges << "]";

    // GPS: distance to company (focus) at pre/at/post
    auto nearestGps = [&](int64_t t) -> const GpsPointLite * {
        if (gps.empty()) {
            return nullptr;
        }
        const GpsPointLite *best = nullptr;
        int64_t bestD = 120000;
        for (const auto &g : gps) {
            const int64_t d = std::llabs(g.t_ms - t);
            if (d < bestD) {
                bestD = d;
                best = &g;
            }
        }
        return best;
    };
    auto gpsSnap = [&](int64_t t, const char *label) -> std::string {
        const GpsPointLite *g = nearestGps(t);
        std::ostringstream o;
        o << "{\"label\":\"" << label << "\",\"t_target_ms\":" << t;
        if (g == nullptr) {
            o << ",\"available\":false}";
            return o.str();
        }
        const double dHome = HaversineM(g->lat, g->lon, anchors.home.lat, anchors.home.lon);
        const double dCo = HaversineM(g->lat, g->lon, anchors.company.lat, anchors.company.lon);
        o << ",\"available\":true,\"t_ms\":" << g->t_ms << ",\"acc_m\":" << g->acc << ",\"dist_home_m\":" << dHome
          << ",\"dist_company_m\":" << dCo << "}";
        return o.str();
    };

    // Mag: simple std of |B| early vs late
    double magEarly = -1.0;
    double magLate = -1.0;
    int magN = 0;
    for (const auto &fp : ListFilesWithPrefix(sessionDir, "mag_data_")) {
        std::ifstream in(fp);
        if (!in.is_open()) {
            continue;
        }
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first) {
                first = false;
                continue;
            }
            const auto cols = SplitCsvFields(line);
            if (cols.size() < 4) {
                continue;
            }
            const int64_t ts = ParseLeadingInt64(cols[0]);
            if (ts < t0 || ts > t1) {
                continue;
            }
            try {
                const double x = std::stod(cols[1]);
                const double y = std::stod(cols[2]);
                const double z = std::stod(cols[3]);
                const double mag = std::sqrt(x * x + y * y + z * z);
                ++magN;
                if (ts <= tCenter) {
                    magEarly = (magEarly < 0) ? mag : 0.9 * magEarly + 0.1 * mag;
                } else {
                    magLate = (magLate < 0) ? mag : 0.9 * magLate + 0.1 * mag;
                }
            } catch (...) {
            }
        }
    }

    PdrSummary pdrSum;
    if (!sessionDir.empty() && PathIsDir(sessionDir)) {
        LoadPdrFromSensorEvents(sessionDir, t0, t1, tCenter, &pdrSum);
        LoadPdrFromSegments(sessionDir, t0, t1, tCenter, &pdrSum);
    }

    std::string liveRadio = "{}";
    std::string livePdr = "{}";
    if (BaselineRuntime::GetInstance().Enabled()) {
        liveRadio = BaselineRuntime::GetInstance().RadioDebugJson(tCenter);
        livePdr = BaselineRuntime::GetInstance().PdrDebugJson();
    }

    std::ostringstream oss;
    oss << "{\"ok\":true,\"t_push_ms\":" << tCenter << ",\"session_dir\":\"" << Esc(sessionDir) << "\""
        << ",\"window_s\":{\"before\":" << beforeS << ",\"after\":" << afterS << "}"
        << ",\"wifi\":{"
        << "\"soft_side\":\"" << wifiRefSide << "\",\"soft_n\":" << soft.size() << ",\"soft_ready\":"
        << (soft.size() >= 2 ? "true" : "false") << ",\"fingerprint_n\":" << fingerprint.size()
        << ",\"fingerprint_ready\":" << (useSiteFp ? "true" : "false") << ",\"wifi_ref_source\":\""
        << wifiRefSource << "\",\"n_scans\":" << scanN << ",\"snapshots\":["
        << wifiSnap(tCenter - 180000, "pre_3min") << "," << wifiSnap(tCenter, "at_push") << ","
        << wifiSnap(tCenter + 300000, "post_5min") << "],\"events\":" << wifiEvents.str() << "}"
        << ",\"cell\":{"
        << "\"n_samples\":" << cells.size() << ",\"dominant_pre\":{\"cell_id\":" << preC.first
        << ",\"n\":" << preC.second << "},\"dominant_at_push\":{\"cell_id\":" << atC.first << ",\"n\":" << atC.second
        << "},\"dominant_post\":{\"cell_id\":" << postC.first << ",\"n\":" << postC.second
        << "},\"changed_around_push\":" << (preC.first != 0 && postC.first != 0 && preC.first != postC.first ? "true" : "false")
        << ",\"changes\":" << cellChanges.str() << "}"
        << ",\"gps\":{\"n_samples\":" << gps.size() << ",\"snapshots\":[" << gpsSnap(tCenter - 180000, "pre_3min")
        << "," << gpsSnap(tCenter, "at_push") << "," << gpsSnap(tCenter + 300000, "post_5min") << "]}"
        << ",\"pdr\":{\"available\":" << (pdrSum.available ? "true" : "false")
        << ",\"n_episodes\":" << pdrSum.episode_count << ",\"n_points\":" << pdrSum.point_count
        << ",\"max_net_m\":" << pdrSum.max_net_m << ",\"max_path_m\":" << pdrSum.max_path_m
        << ",\"net_pre_3min_m\":" << pdrSum.net_pre << ",\"net_at_push_m\":" << pdrSum.net_at_push
        << ",\"path_at_push_m\":" << pdrSum.path_at_push << ",\"straightness_at_push\":" << pdrSum.straightness_at_push
        << ",\"net_post_5min_m\":" << pdrSum.net_post
        << ",\"outbound_hint\":" << (pdrSum.net_at_push >= 8.0 ? "true" : "false")
        << ",\"notes\":\"net = planar distance from walk-episode origin (not distance-to-home)\"}"
        << ",\"mag\":{\"n_samples\":" << magN << ",\"mag_ema_pre\":" << magEarly << ",\"mag_ema_post\":" << magLate
        << ",\"delta\":" << ((magEarly >= 0 && magLate >= 0) ? (magLate - magEarly) : 0.0) << "}"
        << ",\"live_radio_debug\":" << liveRadio << ",\"live_pdr_debug\":" << livePdr
        << ",\"notes\":\"Semantic summary for θ personalizer; wifi_ref_source=company_fingerprint "
           "uses site coverage (same as HSMM) when fingerprint is present; else radio_soft Jaccard.\"}";
    return oss.str();
}

std::string EvidenceQuery::GetWifiWindowJson(const std::string &paramsJson) const
{
    return SensorWindowJson(paramsJson, "wifi_data_", "wifi");
}

std::string EvidenceQuery::GetCellWindowJson(const std::string &paramsJson) const
{
    return SensorWindowJson(paramsJson, "cell_data_", "cell");
}

std::string EvidenceQuery::GetMagWindowJson(const std::string &paramsJson) const
{
    return SensorWindowJson(paramsJson, "mag_data_", "mag");
}

std::string EvidenceQuery::GetGpsWindowJson(const std::string &paramsJson) const
{
    return SensorWindowJson(paramsJson, "location_data_", "gps");
}

}  // namespace commute_sa
