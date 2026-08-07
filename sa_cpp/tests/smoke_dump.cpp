#include "commute_sa/crs.h"
#include "commute_sa/rolling_sensor_dump.h"
#include "commute_sa/sensor_events_writer.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

int main()
{
    // True home (map GCJ) -> WGS84 should be ~40.00988, 116.32064
    const auto homeWgs = commute_sa::Gcj02ToWgs84(40.011181, 116.32677);
    std::cout << "home_wgs84=" << homeWgs.latitude << "," << homeWgs.longitude << "\n";
    if (std::abs(homeWgs.latitude - 40.00988) > 0.001 || std::abs(homeWgs.longitude - 116.32064) > 0.001) {
        std::cerr << "CRS round-trip sanity check failed\n";
        return 1;
    }

    const fs::path outRoot = fs::temp_directory_path() / "commute_sa_smoke";
    fs::create_directories(outRoot);

    commute_sa::RollingSensorDump dump(outRoot.string());
    if (!dump.Start()) {
        std::cerr << "RollingSensorDump::Start failed\n";
        return 2;
    }
    dump.SetPowerMode("HIGH_WALKING");
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                         .count();
    dump.EnqueueLocationWgs84(now, homeWgs.latitude, homeWgs.longitude, 5.0, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const std::string session = dump.SessionDir();
    dump.Stop();

    commute_sa::SensorEventsWriter events((outRoot / "sa_sensor").string());
    if (!events.Start()) {
        std::cerr << "SensorEventsWriter::Start failed\n";
        return 3;
    }
    commute_sa::RawGpsLocation loc;
    loc.latitude = homeWgs.latitude;
    loc.longitude = homeWgs.longitude;
    loc.horizontal_accuracy_m = 5.0;
    loc.has_horizontal_accuracy = true;
    loc.valid = true;
    loc.source_type = 1;
    events.OnGpsLocation(loc);
    events.OnWalkingStarted(now);
    events.Flush();
    const std::string runDir = events.RunDir();
    events.Stop();

    std::cout << "dump_session=" << session << "\n";
    std::cout << "events_run=" << runDir << "\n";
    std::cout << "ok\n";
    return 0;
}
