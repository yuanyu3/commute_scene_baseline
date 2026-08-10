#pragma once

#include "commute_sa/anchors.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace commute_sa {

/**
 * On-device / host executor for anchor_reestimate_jobs.jsonl.
 *
 * Same idea as python/commute_baseline/anchors.py:
 * daytime still-cluster → company; night/sticky still-cluster → home.
 * GPS: session location_data_*.csv under product root + leave_window_samples.jsonl.
 *
 * When it runs (docs/ANCHOR_INFERENCE.md):
 * 1. Immediately after request_anchor_reestimate queues a job
 * 2. On DAY_END personalize tick (drain remaining queued jobs)
 * 3. Host/offline: ProcessQueuedAnchorReestimateJobs(root)
 */
std::string ProcessQueuedAnchorReestimateJobs(const std::string &rootDir, int maxJobs = 2);

using GpsMsLatLon = std::pair<int64_t, std::pair<double, double>>;  // t_ms, (lat, lon)

bool InferAnchorsFromGpsPoints(const std::vector<GpsMsLatLon> &points, const std::string &which,
    const AnchorSet &base, AnchorSet *out, std::string *err);

}  // namespace commute_sa
