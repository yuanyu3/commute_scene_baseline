#include "commute_sa/anchors.h"

#include <fstream>
#include <regex>
#include <sstream>

namespace commute_sa {
namespace {

bool ReadFile(const std::string &path, std::string *out)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

bool ExtractObject(const std::string &json, const std::string &key, std::string *obj)
{
    const std::string pat = "\"" + key + "\"";
    const auto pos = json.find(pat);
    if (pos == std::string::npos) {
        return false;
    }
    const auto brace = json.find('{', pos);
    if (brace == std::string::npos) {
        return false;
    }
    int depth = 0;
    for (size_t i = brace; i < json.size(); ++i) {
        if (json[i] == '{') {
            ++depth;
        } else if (json[i] == '}') {
            --depth;
            if (depth == 0) {
                *obj = json.substr(brace, i - brace + 1);
                return true;
            }
        }
    }
    return false;
}

bool ExtractString(const std::string &json, const std::string &key, std::string *val)
{
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (!std::regex_search(json, m, re)) {
        return false;
    }
    *val = m[1].str();
    return true;
}

bool ExtractDouble(const std::string &json, const std::string &key, double *val)
{
    std::regex re("\"" + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch m;
    if (!std::regex_search(json, m, re)) {
        return false;
    }
    *val = std::stod(m[1].str());
    return true;
}

bool ParseAnchor(const std::string &obj, Anchor *a)
{
    ExtractString(obj, "id", &a->id);
    ExtractString(obj, "method", &a->method);
    if (!ExtractDouble(obj, "lat", &a->lat) || !ExtractDouble(obj, "lon", &a->lon)) {
        return false;
    }
    ExtractDouble(obj, "r_in_m", &a->r_in_m);
    ExtractDouble(obj, "r_out_m", &a->r_out_m);
    return true;
}

}  // namespace

AnchorSet DefaultAnchors()
{
    AnchorSet a;
    a.coordinate_system = "WGS84";
    a.home = {"home_001", 40.00987016905874, 116.32064248647265, 50.0, 90.0, "default"};
    a.company = {"company_001", 40.05395299571119, 116.1735028989794, 50.0, 90.0, "default"};
    a.notes = "built-in default; replace via LoadAnchorsFromFile";
    return a;
}

bool LoadAnchorsFromFile(const std::string &path, AnchorSet *out, std::string *err)
{
    if (out == nullptr) {
        if (err) {
            *err = "out is null";
        }
        return false;
    }
    std::string json;
    if (!ReadFile(path, &json)) {
        if (err) {
            *err = "cannot read " + path;
        }
        return false;
    }
    AnchorSet a = DefaultAnchors();
    ExtractString(json, "coordinate_system", &a.coordinate_system);
    ExtractString(json, "updated_at", &a.updated_at);
    ExtractString(json, "notes", &a.notes);
    std::string homeObj;
    std::string companyObj;
    if (!ExtractObject(json, "home", &homeObj) || !ParseAnchor(homeObj, &a.home)) {
        if (err) {
            *err = "invalid home object";
        }
        return false;
    }
    if (!ExtractObject(json, "company", &companyObj) || !ParseAnchor(companyObj, &a.company)) {
        if (err) {
            *err = "invalid company object";
        }
        return false;
    }
    *out = std::move(a);
    return true;
}

}  // namespace commute_sa
