cmake_minimum_required(VERSION 3.16)
project(commute_sa LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(commute_sa
    src/crs.cpp
    src/rolling_sensor_dump.cpp
    src/sensor_events_writer.cpp
    src/anchors.cpp
    src/theta.cpp
    src/leave_hsmm.cpp
    src/scene_engine.cpp
    src/baseline_runtime.cpp
    src/radio_evidence.cpp
    src/pdr_evidence.cpp
    src/baro_evidence.cpp
    src/anchor_reestimate.cpp
    src/personalization.cpp
    src/personalization_optimizer.cpp
    src/context_template.cpp
    src/personalization_policy.cpp
    src/product_store.cpp
    src/evidence_query.cpp
    src/action_ops.cpp
    src/theta_eval.cpp
)
target_include_directories(commute_sa PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(WIN32)
    target_compile_definitions(commute_sa PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

option(COMMUTE_SA_BUILD_SMOKE "Build host smoke tests" ON)
if(COMMUTE_SA_BUILD_SMOKE)
    add_executable(commute_sa_smoke tests/smoke_dump.cpp)
    target_link_libraries(commute_sa_smoke PRIVATE commute_sa)

    add_executable(commute_scene_smoke tests/smoke_scene_engine.cpp)
    target_link_libraries(commute_scene_smoke PRIVATE commute_sa)

    add_executable(commute_hsmm_smoke tests/smoke_leave_hsmm.cpp)
    target_link_libraries(commute_hsmm_smoke PRIVATE commute_sa)

    add_executable(commute_radio_smoke tests/smoke_radio_evidence.cpp)
    target_link_libraries(commute_radio_smoke PRIVATE commute_sa)

    add_executable(commute_pdr_smoke tests/smoke_pdr_evidence.cpp)
    target_link_libraries(commute_pdr_smoke PRIVATE commute_sa)

    add_executable(commute_baro_smoke tests/smoke_baro_evidence.cpp)
    target_link_libraries(commute_baro_smoke PRIVATE commute_sa)

    add_executable(commute_gapfill_smoke tests/smoke_gapfill.cpp)
    target_link_libraries(commute_gapfill_smoke PRIVATE commute_sa)

    add_executable(commute_evidence_smoke tests/smoke_evidence_query.cpp)
    target_link_libraries(commute_evidence_smoke PRIVATE commute_sa)

    add_executable(commute_action_smoke tests/smoke_action_ops.cpp)
    target_link_libraries(commute_action_smoke PRIVATE commute_sa)

    add_executable(commute_personalization_optimizer_smoke tests/smoke_personalization_optimizer.cpp)
    target_link_libraries(commute_personalization_optimizer_smoke PRIVATE commute_sa)

    add_executable(commute_offline_tools tests/offline_tools_harness.cpp)
    target_link_libraries(commute_offline_tools PRIVATE commute_sa)
endif()
