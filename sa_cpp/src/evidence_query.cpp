#include "commute_sa/evidence_query.h"

#include "commute_sa/product_store.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
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
            !ListFilesWithPrefix(d, "cell_data_").empty() || !ListFilesWithPrefix(d, "mag_data_").empty()) {
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
                    // Defaults if theta unknown in this reader: 90 / 240
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
        << ",\"n_missed_leave\":0,\"n_lead_samples\":" << leads.size()
        << ",\"n_lead_late\":" << nLeadLate << ",\"n_lead_ok\":" << nLeadOk << ",\"n_lead_early\":" << nLeadEarly
        << ",\"lead_p50_s\":" << pct(0.5) << ",\"lead_p90_s\":" << pct(0.9)
        << ",\"notes\":\"lead_s = t_star_outside - t_push; target lead_min_s..lead_max_s (default 90..240)\"}";
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
