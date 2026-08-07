#pragma once

#include <string>

namespace commute_sa {

struct Anchor {
    std::string id;
    double lat = 0.0;
    double lon = 0.0;
    double r_in_m = 50.0;
    double r_out_m = 90.0;
    std::string method;
};

struct AnchorSet {
    std::string coordinate_system = "WGS84";
    Anchor home;
    Anchor company;
    std::string updated_at;
    std::string notes;
};

/** Load anchors.json (WGS84). Returns false on parse/IO failure. */
bool LoadAnchorsFromFile(const std::string &path, AnchorSet *out, std::string *err = nullptr);

/** Built-in fallback matching config/anchors.json sample. */
AnchorSet DefaultAnchors();

}  // namespace commute_sa
