# Embedded PNG copies of the support panel's social logos.
set(PSVR2PT_DONATE_DIR "${PROJECT_SOURCE_DIR}/donate")
configure_file(resources.rc.in "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" @ONLY)

add_executable(PSVR2PassthroughConfig WIN32
    main.cpp
    config_window.cpp
    config_window.h
    update_checker.cpp
    update_checker.h
    support_adapter.cpp
    social_icons.cpp
    social_icons.h
    version.h
    "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
)

target_link_libraries(PSVR2PassthroughConfig PRIVATE
    adamch_support
    psvr2pt_config
    psvr2pt_core
    imgui::imgui
    d3d11
    dxgi
    d3dcompiler
    winhttp
    shell32
    ole32
    windowscodecs
)

target_compile_definitions(PSVR2PassthroughConfig PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

set_target_properties(PSVR2PassthroughConfig PROPERTIES
    OUTPUT_NAME "PSVR2PassthroughConfig"
)

# Loads every embedded social logo into a WARP Direct3D 11 device (no window or GPU needed).
add_executable(social_icon_tests
    ${PROJECT_SOURCE_DIR}/tests/social_icon_tests.cpp
    social_icons.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
)
target_include_directories(social_icon_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(social_icon_tests PRIVATE adamch_support d3d11 ole32 windowscodecs)
target_compile_definitions(social_icon_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
add_test(NAME social_icons COMMAND social_icon_tests)
