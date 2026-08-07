#pragma once

#include <cstdint>
#include <string>

namespace commute_sa {

/**
 * Read-only evidence for θ personalizer Agent tools.
 * Reads ProductStore files + Ability session CSVs under the same product root.
 */
class EvidenceQuery {
public:
    static EvidenceQuery &GetInstance();

    /** Defaults to ProductStore::RootDir() when empty after Init. */
    void SetRootDir(const std::string &rootDir);
    const std::string &RootDir() const;

    /** Current theta.json (or engine defaults message). */
    std::string GetThetaJson() const;

    /** Current anchors.json */
    std::string GetAnchorsJson() const;

    /**
     * Aggregate leave_episodes.jsonl.
     * params JSON: {"since_ms":0,"scene":"ALL"}  scene unused for now except echoed.
     */
    std::string GetErrorStatsJson(const std::string &paramsJson) const;

    /**
     * Push + label rows for one episode.
     * params: {"t_push_ms":123} or {} for latest push.
     */
    std::string GetLeaveEpisodeJson(const std::string &paramsJson) const;

    /**
     * Sparse GPS/walk samples in leave window.
     * params: {"t_push_ms":123,"limit":100}
     */
    std::string GetLeaveWindowSamplesJson(const std::string &paramsJson) const;

    /**
     * Raw sensor window from latest (or given) Ability session dump.
     * params: {"t_center_ms":0,"before_s":600,"after_s":1200,"session_dir":"","limit":200}
     * t_center_ms=0 → use latest push t_push_ms from leave_episodes.
     * limit splits ~half before / half after center (pre keeps nearest-to-center rows).
     */
    std::string GetWifiWindowJson(const std::string &paramsJson) const;
    std::string GetCellWindowJson(const std::string &paramsJson) const;
    std::string GetMagWindowJson(const std::string &paramsJson) const;
    std::string GetGpsWindowJson(const std::string &paramsJson) const;

private:
    EvidenceQuery() = default;

    std::string root_;
    std::string ResolveRoot() const;
    std::string ReadTextFile(const std::string &path) const;
    std::string SensorWindowJson(const std::string &paramsJson, const char *filePrefix,
        const char *sensorName) const;
};

}  // namespace commute_sa
