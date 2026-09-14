add_library(psvr2pt_core STATIC
    camera_source.cpp
    camera_source.h
    camera_geometry.h
    logging.cpp
    logging.h
    input_binding.cpp
    input_binding.h
)

target_include_directories(psvr2pt_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/openvr
)

target_link_libraries(psvr2pt_core PUBLIC
    spdlog::spdlog
    fmt::fmt
    dinput8
    dxguid
    xinput9_1_0
    rpcrt4
)

target_compile_definitions(psvr2pt_core PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)
