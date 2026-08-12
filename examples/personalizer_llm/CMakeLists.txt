# Copyright (c) 2026. Host personalizer LLM (WSL + libbbpjiuwen.so).
cmake_minimum_required(VERSION 3.16)
project(personalizer_llm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(JIUWEN_ROOT "${REPO_ROOT}/../bbpjiuwen-linux" CACHE PATH "bbpjiuwen-linux root for headers")
if(DEFINED ENV{JIUWEN_ROOT} AND NOT "$ENV{JIUWEN_ROOT}" STREQUAL "")
  set(JIUWEN_ROOT "$ENV{JIUWEN_ROOT}" CACHE PATH "bbpjiuwen-linux root for headers" FORCE)
endif()
get_filename_component(JIUWEN_ROOT "${JIUWEN_ROOT}" ABSOLUTE)

set(JIUWEN_LIB_DIR "${REPO_ROOT}/third_party/bbpjiuwen/lib")
if(NOT EXISTS "${JIUWEN_LIB_DIR}/libbbpjiuwen.so")
  set(JIUWEN_LIB_DIR "${JIUWEN_ROOT}/lib")
endif()
set(JIUWEN_SO "${JIUWEN_LIB_DIR}/libbbpjiuwen.so")
if(NOT EXISTS "${JIUWEN_SO}")
  message(FATAL_ERROR "Missing ${JIUWEN_SO}. Run scripts/sync_jiuwen_libs.sh or set JIUWEN_ROOT")
endif()

set(COMMUTE_SA_ROOT "${REPO_ROOT}/sa_cpp")

add_library(commute_sa_host STATIC
  "${COMMUTE_SA_ROOT}/src/anchors.cpp"
  "${COMMUTE_SA_ROOT}/src/theta.cpp"
  "${COMMUTE_SA_ROOT}/src/leave_hsmm.cpp"
  "${COMMUTE_SA_ROOT}/src/scene_engine.cpp"
  "${COMMUTE_SA_ROOT}/src/baseline_runtime.cpp"
  "${COMMUTE_SA_ROOT}/src/radio_evidence.cpp"
  "${COMMUTE_SA_ROOT}/src/pdr_evidence.cpp"
  "${COMMUTE_SA_ROOT}/src/personalization.cpp"
  "${COMMUTE_SA_ROOT}/src/personalization_policy.cpp"
  "${COMMUTE_SA_ROOT}/src/product_store.cpp"
  "${COMMUTE_SA_ROOT}/src/evidence_query.cpp"
  "${COMMUTE_SA_ROOT}/src/action_ops.cpp"
  "${COMMUTE_SA_ROOT}/src/theta_eval.cpp"
  "${COMMUTE_SA_ROOT}/src/anchor_reestimate.cpp"
)
target_include_directories(commute_sa_host PUBLIC "${COMMUTE_SA_ROOT}/include")

add_executable(personalizer_llm
  main.cpp
  register_tools.cpp
)
target_include_directories(personalizer_llm PRIVATE
  "${JIUWEN_ROOT}/src/include"
  "${JIUWEN_ROOT}/src"
  "${COMMUTE_SA_ROOT}/include"
)
target_compile_options(personalizer_llm PRIVATE -stdlib=libc++)
target_link_options(personalizer_llm PRIVATE -stdlib=libc++)
target_link_libraries(personalizer_llm PRIVATE
  commute_sa_host
  "${JIUWEN_SO}"
  c++
  c++abi
  pthread
  dl
)
set_target_properties(personalizer_llm PROPERTIES
  BUILD_RPATH "${JIUWEN_LIB_DIR}"
  INSTALL_RPATH "${JIUWEN_LIB_DIR}"
)
message(STATUS "JIUWEN_ROOT=${JIUWEN_ROOT}")
message(STATUS "Linking ${JIUWEN_SO}")
