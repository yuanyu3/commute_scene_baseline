#pragma once

#include "commute_sa/geo.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace commute_sa {

/**
 * Online PDR leave evidence for SceneEngine.
 *
 * Matches the offline baseline + LeavingHomeBaseline semantics:
 * - net_displacement_m = planar distance from walk-episode origin to current PDR (x,y) meters
 * - NOT distance-to-HOME; attributed to home/company by walk-start (or first GPS) relation
 * - HSMM observation uses: sPdr = clip01(net / max(15, r_in*0.3)); hit if sPdr>=0.5; ETA boost if net>=8
 */
struct PdrLeaveSnapshot {
    double net_displacement_m = 0.0;
    double path_length_m = 0.0;
    double straightness = 0.0;  // net / path, 0..1
    double pdr_net_out_home_m = 0.0;
    double pdr_net_out_company_m = 0.0;
    bool walking = false;
    bool has_origin = false;
    int point_count = 0;
    bool tagged_home = false;
    bool tagged_company = false;
    std::string reason;
};

class PdrEvidence {
public:
    void Reset();

    void OnWalkingStarted(int64_t tMs);
    void OnWalkingStopped(int64_t tMs);

    /** Local planar meters from PDR library (GetCx/GetCy). */
    void OnPdrPoint(int64_t tMs, double xM, double yM);

    /**
     * Attribute this walk to home and/or company leave.
     * Call when walk starts or on first usable GPS tick while walking.
     * INSIDE/NEAR → tag that side; already-tagged sides stick for the episode.
     */
    void NoteWalkContext(Relation homeRel, Relation companyRel);

    PdrLeaveSnapshot Evaluate() const;

    std::string DebugJson() const;

private:
    mutable std::mutex mutex_;
    bool walking_ = false;
    bool has_origin_ = false;
    int64_t walk_started_ms_ = 0;
    double origin_x_ = 0.0;
    double origin_y_ = 0.0;
    double last_x_ = 0.0;
    double last_y_ = 0.0;
    double path_length_m_ = 0.0;
    int point_count_ = 0;
    bool tagged_home_ = false;
    bool tagged_company_ = false;
};

}  // namespace commute_sa
