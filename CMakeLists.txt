cmake_minimum_required(VERSION 3.24)

project(morpheus VERSION 0.1.0 LANGUAGES C OBJC)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(ExternalProject)
find_program(MORPHEUS_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
find_program(MORPHEUS_PATCH_EXECUTABLE NAMES patch REQUIRED)
find_program(MORPHEUS_CODESIGN_EXECUTABLE NAMES codesign REQUIRED)
set(
    MORPHEUS_AGENT_PROVIDER
    "${CMAKE_SOURCE_DIR}/tools/morpheus-codex-agent"
    CACHE FILEPATH
    "Executable implementing the Morpheus external coding-agent protocol"
)

set(SDL_SHARED OFF CACHE BOOL "Build SDL as a shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL as a static library" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build the SDL test library" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL tests" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "Build SDL examples" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "Install SDL" FORCE)
add_subdirectory(SDL EXCLUDE_FROM_ALL)

set(MORPHEUS_TCC_BUILD_DIR "${CMAKE_BINARY_DIR}/tinycc-build")
set(MORPHEUS_TCC_SOURCE_DIR "${CMAKE_BINARY_DIR}/tinycc-src")
set(MORPHEUS_TCC_LIBRARY "${MORPHEUS_TCC_BUILD_DIR}/libtcc.a")

ExternalProject_Add(morpheus_tinycc
    SOURCE_DIR "${MORPHEUS_TCC_SOURCE_DIR}"
    BINARY_DIR "${MORPHEUS_TCC_BUILD_DIR}"
    DOWNLOAD_COMMAND
        "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_SOURCE_DIR}/tinycc"
        "${MORPHEUS_TCC_SOURCE_DIR}"
    UPDATE_COMMAND ""
    PATCH_COMMAND
        "${MORPHEUS_PATCH_EXECUTABLE}" -p1
        -i "${CMAKE_SOURCE_DIR}/patches/tinycc-map-jit.patch"
    CONFIGURE_COMMAND
        "${CMAKE_SOURCE_DIR}/tinycc/configure"
        "--source-path=../tinycc-src"
        "--cc=${CMAKE_C_COMPILER}"
        --enable-static
    BUILD_COMMAND "${MORPHEUS_MAKE_EXECUTABLE}" libtcc.a
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${MORPHEUS_TCC_LIBRARY}"
)

add_library(morpheus_libtcc STATIC IMPORTED GLOBAL)
set_target_properties(morpheus_libtcc PROPERTIES
    IMPORTED_LOCATION "${MORPHEUS_TCC_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/tinycc"
)
add_dependencies(morpheus_libtcc morpheus_tinycc)

add_executable(morpheus MACOSX_BUNDLE
    src/host/main.m
    src/agent/agent_session.c
    src/compiler/runtime_module.c
    src/project/revision_store.c
)
add_dependencies(morpheus morpheus_tinycc)

target_include_directories(morpheus PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/Nuklear"
    "${CMAKE_SOURCE_DIR}/Nuklear/demo/metal"
    "${CMAKE_SOURCE_DIR}/src/agent"
    "${CMAKE_SOURCE_DIR}/src/compiler"
    "${CMAKE_SOURCE_DIR}/src/project"
    "${CMAKE_SOURCE_DIR}/tinycc"
)

target_compile_definitions(morpheus PRIVATE
    MORPHEUS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    MORPHEUS_AGENT_PROVIDER_PATH="${MORPHEUS_AGENT_PROVIDER}"
    MORPHEUS_TCC_LIB_PATH="${MORPHEUS_TCC_BUILD_DIR}"
)

target_compile_options(morpheus PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>
    $<$<COMPILE_LANGUAGE:OBJC>:-Wno-c23-extensions>
)

target_link_libraries(morpheus PRIVATE
    morpheus_libtcc
    SDL3::SDL3-static
    "-framework Metal"
    "-framework QuartzCore"
    "-framework Foundation"
)

set_target_properties(morpheus PROPERTIES
    OUTPUT_NAME "Morpheus"
    MACOSX_BUNDLE_BUNDLE_NAME "Morpheus"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.morpheus.seed"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/MorpheusInfo.plist.in"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

add_custom_target(morpheus_hardened
    COMMAND "${MORPHEUS_CODESIGN_EXECUTABLE}"
        --force
        --sign -
        --options runtime
        --entitlements "${CMAKE_SOURCE_DIR}/cmake/Morpheus.entitlements"
        "$<TARGET_BUNDLE_DIR:morpheus>"
    COMMAND "${MORPHEUS_CODESIGN_EXECUTABLE}"
        --verify
        --deep
        --strict
        --verbose=2
        "$<TARGET_BUNDLE_DIR:morpheus>"
    DEPENDS morpheus
    VERBATIM
    USES_TERMINAL
)

if(BUILD_TESTING)
    add_executable(morpheus_runtime_reload_test
        tests/runtime_reload_test.c
        src/compiler/runtime_module.c
    )
    add_dependencies(morpheus_runtime_reload_test morpheus_tinycc)
    target_include_directories(morpheus_runtime_reload_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src/compiler"
        "${CMAKE_SOURCE_DIR}/tinycc"
    )
    target_compile_definitions(morpheus_runtime_reload_test PRIVATE
        MORPHEUS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
        MORPHEUS_TCC_LIB_PATH="${MORPHEUS_TCC_BUILD_DIR}"
        MORPHEUS_TEST_FIXTURE_ROOT="${CMAKE_SOURCE_DIR}/tests/fixtures"
    )
    target_compile_options(morpheus_runtime_reload_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_runtime_reload_test PRIVATE
        morpheus_libtcc
    )
    add_test(NAME runtime_reload COMMAND morpheus_runtime_reload_test)

    add_executable(morpheus_revision_store_test
        tests/revision_store_test.c
        src/project/revision_store.c
    )
    target_include_directories(morpheus_revision_store_test PRIVATE
        "${CMAKE_SOURCE_DIR}/src/project"
    )
    target_compile_options(morpheus_revision_store_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME revision_store COMMAND morpheus_revision_store_test)

    add_executable(morpheus_fake_agent_provider
        tests/fixtures/fake_agent_provider.c
    )
    target_compile_options(morpheus_fake_agent_provider PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_executable(morpheus_agent_session_test
        tests/agent_session_test.c
        src/agent/agent_session.c
    )
    target_include_directories(morpheus_agent_session_test PRIVATE
        "${CMAKE_SOURCE_DIR}/src/agent"
    )
    target_compile_definitions(morpheus_agent_session_test PRIVATE
        MORPHEUS_FAKE_AGENT_PATH="$<TARGET_FILE:morpheus_fake_agent_provider>"
    )
    target_compile_options(morpheus_agent_session_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_dependencies(morpheus_agent_session_test morpheus_fake_agent_provider)
    add_test(NAME agent_session COMMAND morpheus_agent_session_test)

    add_executable(morpheus_fake_repair_agent_provider
        tests/fixtures/fake_repair_agent_provider.c
    )
    target_compile_definitions(morpheus_fake_repair_agent_provider PRIVATE
        MORPHEUS_VALID_MODULE="${CMAKE_SOURCE_DIR}/tests/fixtures/module_v1.c"
        MORPHEUS_INVALID_MODULE="${CMAKE_SOURCE_DIR}/tests/fixtures/module_compile_error.c"
    )
    target_compile_options(morpheus_fake_repair_agent_provider PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_executable(morpheus_agent_repair_loop_test
        tests/agent_repair_loop_test.c
        src/agent/agent_session.c
        src/compiler/runtime_module.c
    )
    add_dependencies(
        morpheus_agent_repair_loop_test
        morpheus_tinycc
        morpheus_fake_repair_agent_provider
    )
    target_include_directories(morpheus_agent_repair_loop_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src/agent"
        "${CMAKE_SOURCE_DIR}/src/compiler"
        "${CMAKE_SOURCE_DIR}/tinycc"
    )
    target_compile_definitions(morpheus_agent_repair_loop_test PRIVATE
        MORPHEUS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
        MORPHEUS_TCC_LIB_PATH="${MORPHEUS_TCC_BUILD_DIR}"
        MORPHEUS_REPAIR_AGENT_PATH="$<TARGET_FILE:morpheus_fake_repair_agent_provider>"
        MORPHEUS_VALID_MODULE="${CMAKE_SOURCE_DIR}/tests/fixtures/module_v1.c"
        MORPHEUS_API_HEADER="${CMAKE_SOURCE_DIR}/include/morpheus/app_api.h"
    )
    target_compile_options(morpheus_agent_repair_loop_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_agent_repair_loop_test PRIVATE
        morpheus_libtcc
    )
    add_test(NAME agent_repair_loop COMMAND morpheus_agent_repair_loop_test)

    add_custom_target(morpheus_hardened_runtime_check
        COMMAND "${MORPHEUS_CODESIGN_EXECUTABLE}"
            --force
            --sign -
            --options runtime
            --entitlements "${CMAKE_SOURCE_DIR}/cmake/Morpheus.entitlements"
            "$<TARGET_FILE:morpheus_runtime_reload_test>"
        COMMAND "${MORPHEUS_CODESIGN_EXECUTABLE}"
            --verify
            --strict
            --verbose=2
            "$<TARGET_FILE:morpheus_runtime_reload_test>"
        COMMAND "$<TARGET_FILE:morpheus_runtime_reload_test>"
        DEPENDS morpheus_runtime_reload_test
        VERBATIM
        USES_TERMINAL
    )
endif()
