cmake_minimum_required(VERSION 3.24)

project(morpheus VERSION 0.1.0 LANGUAGES C OBJC)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party libraries statically" FORCE)
option(MORPHEUS_ENABLE_RUNTIME_LEAKCHECK
    "Track TinyCC generated-module allocations with stb_leakcheck"
    OFF)

set(MORPHEUS_FROZEN_SOURCE "${CMAKE_SOURCE_DIR}/generated/app.c"
    CACHE FILEPATH "Accepted generated C source for the frozen export")
set(MORPHEUS_EXPORT_APP_NAME "Morpheus App"
    CACHE STRING "Display name for the frozen application")
set(MORPHEUS_EXPORT_BUNDLE_ID "dev.morpheus.exported"
    CACHE STRING "Bundle identifier for the frozen application")
set(MORPHEUS_EXPORT_APP_VERSION "1.0.0"
    CACHE STRING "Version for the frozen application")
set(MORPHEUS_EXPORT_ASSETS "${CMAKE_SOURCE_DIR}/generated/assets"
    CACHE PATH "Asset directory copied into the frozen application")

# Keep export metadata synchronized with the public ABI header.
file(STRINGS "${CMAKE_SOURCE_DIR}/include/morpheus/app_api.h"
    MORPHEUS_HOST_ABI_LINE
    REGEX "^#define MORPHEUS_HOST_ABI_VERSION [0-9]+u$")
file(STRINGS "${CMAKE_SOURCE_DIR}/include/morpheus/app_api.h"
    MORPHEUS_APP_ABI_LINE
    REGEX "^#define MORPHEUS_APP_ABI_VERSION [0-9]+u$")
string(REGEX REPLACE ".* ([0-9]+)u$" "\\1"
    MORPHEUS_HOST_ABI_VERSION_VALUE "${MORPHEUS_HOST_ABI_LINE}")
string(REGEX REPLACE ".* ([0-9]+)u$" "\\1"
    MORPHEUS_APP_ABI_VERSION_VALUE "${MORPHEUS_APP_ABI_LINE}")
if(MORPHEUS_EXPORT_APP_NAME MATCHES "[\"\\\\\r\n]" OR
   NOT MORPHEUS_EXPORT_BUNDLE_ID MATCHES "^[A-Za-z0-9.-]+$" OR
   NOT MORPHEUS_EXPORT_APP_VERSION MATCHES "^[A-Za-z0-9.-]+$")
    message(FATAL_ERROR "Frozen export metadata contains unsupported characters")
endif()

file(STRINGS
    "${CMAKE_SOURCE_DIR}/Nuklear/nuklear.h"
    MORPHEUS_NUKLEAR_API_LINES
    REGEX "^NK_API[ \t]+.*nk_[A-Za-z0-9_]+[ \t]*\\("
)
set(MORPHEUS_NUKLEAR_SYMBOLS)
foreach(MORPHEUS_NUKLEAR_API_LINE IN LISTS MORPHEUS_NUKLEAR_API_LINES)
    string(
        REGEX REPLACE
        "^.*(nk_[A-Za-z0-9_]+)[ \t]*\\(.*$"
        "\\1"
        MORPHEUS_NUKLEAR_SYMBOL
        "${MORPHEUS_NUKLEAR_API_LINE}"
    )
    list(APPEND MORPHEUS_NUKLEAR_SYMBOLS "${MORPHEUS_NUKLEAR_SYMBOL}")
endforeach()
list(REMOVE_DUPLICATES MORPHEUS_NUKLEAR_SYMBOLS)
list(SORT MORPHEUS_NUKLEAR_SYMBOLS)
set(MORPHEUS_NUKLEAR_SYMBOL_FILE "")
foreach(MORPHEUS_NUKLEAR_SYMBOL IN LISTS MORPHEUS_NUKLEAR_SYMBOLS)
    string(APPEND MORPHEUS_NUKLEAR_SYMBOL_FILE
        "MORPHEUS_NUKLEAR_SYMBOL(${MORPHEUS_NUKLEAR_SYMBOL})\n")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(WRITE
    "${CMAKE_BINARY_DIR}/generated/morpheus_nuklear_symbols.inc"
    "${MORPHEUS_NUKLEAR_SYMBOL_FILE}"
)

include(ExternalProject)
find_program(MORPHEUS_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
find_program(MORPHEUS_PATCH_EXECUTABLE NAMES patch REQUIRED)
find_program(MORPHEUS_CODESIGN_EXECUTABLE NAMES codesign REQUIRED)
find_program(MORPHEUS_STRIP_EXECUTABLE NAMES strip REQUIRED)
find_program(MORPHEUS_XCRUN_EXECUTABLE NAMES xcrun REQUIRED)
execute_process(
    COMMAND "${MORPHEUS_XCRUN_EXECUTABLE}" --find actool
    RESULT_VARIABLE MORPHEUS_ACTOOL_RESULT
    OUTPUT_VARIABLE MORPHEUS_ACTOOL_EXECUTABLE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT MORPHEUS_ACTOOL_RESULT EQUAL 0 OR
   NOT EXISTS "${MORPHEUS_ACTOOL_EXECUTABLE}")
    message(FATAL_ERROR
        "A full Xcode installation with actool is required to build the Morpheus icon")
endif()
set(
    MORPHEUS_AGENT_PROVIDER
    "${CMAKE_SOURCE_DIR}/tools/morpheus-codex-agent"
    CACHE FILEPATH
    "Executable implementing the Morpheus external coding-agent protocol"
)

set(MORPHEUS_ICON_SOURCE "${CMAKE_SOURCE_DIR}/docs/icons/morpheus.icon")
set(MORPHEUS_ICON_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/morpheus-icon")
set(MORPHEUS_ICON_ASSET_CATALOG "${MORPHEUS_ICON_OUTPUT_DIR}/Assets.car")
set(MORPHEUS_ICON_ICNS "${MORPHEUS_ICON_OUTPUT_DIR}/morpheus.icns")
set(MORPHEUS_ICON_PARTIAL_PLIST "${MORPHEUS_ICON_OUTPUT_DIR}/partial.plist")
file(GLOB_RECURSE MORPHEUS_ICON_INPUTS CONFIGURE_DEPENDS
    "${MORPHEUS_ICON_SOURCE}/*")
if(NOT EXISTS "${MORPHEUS_ICON_SOURCE}/icon.json" OR
   NOT MORPHEUS_ICON_INPUTS)
    message(FATAL_ERROR "Morpheus Icon Composer source is incomplete: ${MORPHEUS_ICON_SOURCE}")
endif()
add_custom_command(
    OUTPUT
        "${MORPHEUS_ICON_ASSET_CATALOG}"
        "${MORPHEUS_ICON_ICNS}"
        "${MORPHEUS_ICON_PARTIAL_PLIST}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${MORPHEUS_ICON_OUTPUT_DIR}"
    COMMAND "${MORPHEUS_ACTOOL_EXECUTABLE}"
        "${MORPHEUS_ICON_SOURCE}"
        --compile "${MORPHEUS_ICON_OUTPUT_DIR}"
        --platform macosx
        --minimum-deployment-target 14.0
        --target-device mac
        --app-icon morpheus
        --output-partial-info-plist "${MORPHEUS_ICON_PARTIAL_PLIST}"
        --output-format human-readable-text
        --notices
        --warnings
    DEPENDS ${MORPHEUS_ICON_INPUTS}
    COMMENT "Compiling the Morpheus Icon Composer asset"
    VERBATIM
)
add_custom_target(morpheus_app_icon DEPENDS
    "${MORPHEUS_ICON_ASSET_CATALOG}"
    "${MORPHEUS_ICON_ICNS}"
    "${MORPHEUS_ICON_PARTIAL_PLIST}"
)
set_source_files_properties(
    "${MORPHEUS_ICON_ASSET_CATALOG}"
    "${MORPHEUS_ICON_ICNS}"
    PROPERTIES
        GENERATED TRUE
        MACOSX_PACKAGE_LOCATION Resources
)

set(SDL_SHARED OFF CACHE BOOL "Build SDL as a shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL as a static library" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "Disable unused audio access" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "Disable unused camera access" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "Disable unused native file dialogs" FORCE)
set(SDL_GPU OFF CACHE BOOL "Disable unused SDL GPU API" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "Disable unused haptic access" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "Disable unused controller access" FORCE)
set(SDL_POWER OFF CACHE BOOL "Disable unused power queries" FORCE)
set(SDL_RENDER OFF CACHE BOOL "Disable unused SDL renderer API" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "Disable unused sensor access" FORCE)
set(SDL_TRAY OFF CACHE BOOL "Disable unused tray integration" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "Disable unused OpenGL backend" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "Disable unused OpenGL ES backend" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "Disable unused Vulkan backend" FORCE)
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

# SQLite's canonical configure build rejects source paths containing spaces.
# Stage its pinned public-domain sources under the platform temporary directory
# so this repository remains buildable from paths such as "External HD".
string(SHA256 MORPHEUS_SQLITE_STAGE_HASH "${CMAKE_SOURCE_DIR}")
string(SUBSTRING "${MORPHEUS_SQLITE_STAGE_HASH}" 0 12 MORPHEUS_SQLITE_STAGE_HASH)
if(APPLE)
    set(MORPHEUS_SQLITE_STAGE_ROOT
        "/private/tmp/morpheus-sqlite-${MORPHEUS_SQLITE_STAGE_HASH}")
else()
    set(MORPHEUS_SQLITE_STAGE_ROOT
        "${CMAKE_BINARY_DIR}/sqlite-stage-${MORPHEUS_SQLITE_STAGE_HASH}")
endif()
set(MORPHEUS_SQLITE_SOURCE_DIR "${MORPHEUS_SQLITE_STAGE_ROOT}/src")
set(MORPHEUS_SQLITE_BUILD_DIR "${MORPHEUS_SQLITE_STAGE_ROOT}/build")
set(MORPHEUS_SQLITE_STAMP_DIR "${MORPHEUS_SQLITE_STAGE_ROOT}/stamp")
set(MORPHEUS_SQLITE_LIBRARY "${MORPHEUS_SQLITE_BUILD_DIR}/libsqlite3.a")
file(MAKE_DIRECTORY "${MORPHEUS_SQLITE_BUILD_DIR}")

ExternalProject_Add(morpheus_sqlite_external
    SOURCE_DIR "${MORPHEUS_SQLITE_SOURCE_DIR}"
    BINARY_DIR "${MORPHEUS_SQLITE_BUILD_DIR}"
    # Keep stamps with the temporary artifacts. If macOS purges the stage,
    # CMake must repeat download and configure rather than trust stale stamps
    # retained in the repository build directory.
    STAMP_DIR "${MORPHEUS_SQLITE_STAMP_DIR}"
    DOWNLOAD_COMMAND
        "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_SOURCE_DIR}/third_party/sqlite"
        "${MORPHEUS_SQLITE_SOURCE_DIR}"
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND
        # The system Tcl shipped by macOS cannot spawn child commands in some
        # managed environments. Build SQLite's bundled bootstrap Jim Tcl and
        # force autosetup to use it instead.
        "${CMAKE_C_COMPILER}" -O2
        -o "${MORPHEUS_SQLITE_BUILD_DIR}/jimsh0"
        "${MORPHEUS_SQLITE_SOURCE_DIR}/autosetup/jimsh0.c"
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "CC=${CMAKE_C_COMPILER}"
        "autosetup_tclsh=${MORPHEUS_SQLITE_BUILD_DIR}/jimsh0"
        "${MORPHEUS_SQLITE_SOURCE_DIR}/configure"
        --disable-shared
        --disable-readline
        --disable-load-extension
        --disable-tcl
    BUILD_COMMAND "${MORPHEUS_MAKE_EXECUTABLE}" libsqlite3.a
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
        "${MORPHEUS_SQLITE_LIBRARY}"
        "${MORPHEUS_SQLITE_BUILD_DIR}/sqlite3.h"
)

add_library(morpheus_sqlite STATIC IMPORTED GLOBAL)
set_target_properties(morpheus_sqlite PROPERTIES
    IMPORTED_LOCATION "${MORPHEUS_SQLITE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MORPHEUS_SQLITE_BUILD_DIR}"
)
add_dependencies(morpheus_sqlite morpheus_sqlite_external)

set(YYJSON_BUILD_TESTS OFF CACHE BOOL "Build yyjson tests" FORCE)
set(YYJSON_BUILD_FUZZER OFF CACHE BOOL "Build yyjson fuzzer" FORCE)
set(YYJSON_BUILD_MISC OFF CACHE BOOL "Build yyjson utilities" FORCE)
set(YYJSON_BUILD_DOC OFF CACHE BOOL "Build yyjson documentation" FORCE)
set(YYJSON_INSTALL OFF CACHE BOOL "Install yyjson" FORCE)
set(YYJSON_DISABLE_INCR_READER ON CACHE BOOL "Disable yyjson incremental reader" FORCE)
set(YYJSON_DISABLE_UTILS ON CACHE BOOL "Disable yyjson patch/pointer utilities" FORCE)
set(YYJSON_DISABLE_NON_STANDARD ON CACHE BOOL "Disable non-standard JSON" FORCE)
add_subdirectory(third_party/yyjson EXCLUDE_FROM_ALL)

set(BUILD_EXAMPLES OFF CACHE BOOL "Build miniz examples" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "Build miniz fuzzers" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Build miniz tests" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "Install miniz" FORCE)
set(BUILD_NO_STDIO ON CACHE BOOL "Build miniz without stdio archive APIs" FORCE)
set(AMALGAMATE_SOURCES OFF CACHE BOOL "Generate miniz amalgamation" FORCE)
set(BUILD_HEADER_ONLY OFF CACHE BOOL "Build miniz header-only" FORCE)
add_subdirectory(third_party/miniz EXCLUDE_FROM_ALL)
target_compile_options(miniz PRIVATE -Wno-unused-function)

# Compile fixed-size la and Morpheus's checked dynamic numeric layer into one
# host-private archive. Generated modules receive neither this target nor its
# vendor types; public math/numeric boundaries are defined separately.
add_library(morpheus_math STATIC
    src/host/la_impl.c
    src/host/numeric.c
)
target_include_directories(morpheus_math PUBLIC
    "${CMAKE_SOURCE_DIR}/third_party/la"
    "${CMAKE_SOURCE_DIR}/src/host"
)
target_compile_options(morpheus_math PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -ffunction-sections
    -fdata-sections
)
set_target_properties(morpheus_math PROPERTIES C_VISIBILITY_PRESET hidden)
if(UNIX)
    target_link_libraries(morpheus_math PUBLIC m)
endif()

add_library(morpheus_stb_image STATIC src/host/stb_image_impl.c)
target_include_directories(morpheus_stb_image PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/stb"
)
target_compile_options(morpheus_stb_image PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Runtime facilities shared by the authoring host and standalone applications.
# Keep this target independent of TinyCC, agents, projects, revisions, and the
# Morpheus authoring UI so frozen applications cannot acquire those components
# transitively.
add_library(morpheus_runtime_core STATIC
    src/host/database_service.c
    src/host/http_service.m
    src/host/image_service.m
    src/host/json_service.c
)
target_include_directories(morpheus_runtime_core
    PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/host"
        "${CMAKE_SOURCE_DIR}/third_party/stb"
)
target_compile_options(morpheus_runtime_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>
    $<$<COMPILE_LANGUAGE:OBJC>:-Wno-c23-extensions>
)
target_link_libraries(morpheus_runtime_core PRIVATE
    morpheus_math
    morpheus_sqlite
    yyjson
    morpheus_stb_image
    "-framework CoreGraphics"
    "-framework Foundation"
    "-framework Metal"
)

# The development host discovers SDK functions by name for TinyCC modules, so
# every runtime-core object must be present even when the host has no direct C
# reference to a particular SDK function. Frozen apps currently ship the same
# complete capability profile. Unit tests link normally and pull only the
# service under test.
function(morpheus_force_link_runtime_core target)
    target_link_libraries(${target} PRIVATE morpheus_runtime_core)
    if(APPLE)
        target_link_options(${target} PRIVATE
            "-Wl,-force_load,$<TARGET_FILE:morpheus_runtime_core>"
        )
    endif()
endfunction()

# Standalone process shell. The generated application and tiny launcher link
# this archive, while the Morpheus authoring shell keeps its own event loop.
add_library(morpheus_standalone_runtime STATIC
    src/runtime/standalone_runtime.m
)
target_include_directories(morpheus_standalone_runtime
    PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/Nuklear/demo/metal"
        "${CMAKE_SOURCE_DIR}/src/host"
)
target_compile_options(morpheus_standalone_runtime PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -ffunction-sections
    -fdata-sections
    -fobjc-arc
    -Wno-c23-extensions
    -Wno-unused-but-set-variable
)
target_link_libraries(morpheus_standalone_runtime PRIVATE
    morpheus_runtime_core
    SDL3::SDL3-static
    "-framework AppKit"
    "-framework CoreGraphics"
    "-framework CoreText"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework Foundation"
)

if(EXISTS "${MORPHEUS_FROZEN_SOURCE}")
file(SHA256 "${MORPHEUS_FROZEN_SOURCE}" MORPHEUS_FROZEN_SOURCE_SHA256)
foreach(MORPHEUS_EXPORT_DEPENDENCY
        SDL Nuklear third_party/sqlite third_party/yyjson third_party/stb
        third_party/la)
    string(REPLACE "/" "_" MORPHEUS_EXPORT_DEPENDENCY_KEY "${MORPHEUS_EXPORT_DEPENDENCY}")
    string(TOUPPER "${MORPHEUS_EXPORT_DEPENDENCY_KEY}" MORPHEUS_EXPORT_DEPENDENCY_KEY)
    execute_process(
        COMMAND git -C "${CMAKE_SOURCE_DIR}/${MORPHEUS_EXPORT_DEPENDENCY}" rev-parse HEAD
        OUTPUT_VARIABLE "MORPHEUS_EXPORT_${MORPHEUS_EXPORT_DEPENDENCY_KEY}_REVISION"
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
endforeach()
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/export-manifest.json.in"
    "${CMAKE_BINARY_DIR}/generated/export-manifest.json"
    @ONLY
)

add_executable(morpheus_frozen MACOSX_BUNDLE EXCLUDE_FROM_ALL
    src/export/main.m
    "${MORPHEUS_FROZEN_SOURCE}"
)
target_include_directories(morpheus_frozen PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/Nuklear"
)
target_compile_definitions(morpheus_frozen PRIVATE
    MORPHEUS_EXPORT_APP_NAME="${MORPHEUS_EXPORT_APP_NAME}"
    MORPHEUS_EXPORT_BUNDLE_ID="${MORPHEUS_EXPORT_BUNDLE_ID}"
)
target_compile_options(morpheus_frozen PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -ffunction-sections
    -fdata-sections
    $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>
    $<$<COMPILE_LANGUAGE:OBJC>:-Wno-c23-extensions>
    $<$<COMPILE_LANGUAGE:OBJC>:-Wno-unused-but-set-variable>
)
target_link_options(morpheus_frozen PRIVATE
    "-Wl,-dead_strip"
)
target_link_libraries(morpheus_frozen PRIVATE
    morpheus_standalone_runtime
)
morpheus_force_link_runtime_core(morpheus_frozen)
set_target_properties(morpheus_frozen PROPERTIES
    OUTPUT_NAME "MorpheusFrozen"
    MACOSX_BUNDLE_BUNDLE_NAME "${MORPHEUS_EXPORT_APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${MORPHEUS_EXPORT_BUNDLE_ID}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/FrozenInfo.plist.in"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${MORPHEUS_EXPORT_APP_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${MORPHEUS_EXPORT_APP_VERSION}"
)
add_custom_command(TARGET morpheus_frozen POST_BUILD
    COMMAND "${MORPHEUS_STRIP_EXECUTABLE}" -S -x
        "$<TARGET_FILE:morpheus_frozen>"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_BUNDLE_CONTENT_DIR:morpheus_frozen>/Resources/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${MORPHEUS_EXPORT_ASSETS}"
        "$<TARGET_BUNDLE_CONTENT_DIR:morpheus_frozen>/Resources/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_BINARY_DIR}/generated/export-manifest.json"
        "$<TARGET_BUNDLE_CONTENT_DIR:morpheus_frozen>/Resources/export-manifest.json"
    COMMAND "${MORPHEUS_CODESIGN_EXECUTABLE}"
        --force
        --deep
        --sign -
        --options runtime
        "$<TARGET_BUNDLE_DIR:morpheus_frozen>"
    COMMAND "${MORPHEUS_CODESIGN_EXECUTABLE}"
        --verify
        --deep
        --strict
        "$<TARGET_BUNDLE_DIR:morpheus_frozen>"
    VERBATIM
)
else()
    message(STATUS
        "Frozen export target disabled; source not found: ${MORPHEUS_FROZEN_SOURCE}")
endif()

# The replaceable authoring UI sees only public application and capability
# headers. It cannot acquire provider implementations through this target.
add_library(morpheus_authoring_ui STATIC
    src/authoring/morpheus_app.c
)
target_include_directories(morpheus_authoring_ui
    PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
)
target_compile_options(morpheus_authoring_ui PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Development-only providers. This archive is deliberately absent from the
# frozen target and never depends on the authoring UI target.
add_library(morpheus_authoring STATIC
    src/authoring/authoring_capabilities.c
    src/authoring/authoring_controller.c
    src/authoring/authoring_shell.c
    src/authoring/export_service.c
    src/agent/agent_session.c
    src/compiler/runtime_module.c
    src/project/revision_store.c
    src/project/project_store.c
)
if(MORPHEUS_ENABLE_RUNTIME_LEAKCHECK)
    target_sources(morpheus_authoring PRIVATE
        src/compiler/runtime_leakcheck.c
    )
endif()
add_dependencies(morpheus_authoring morpheus_tinycc)
target_include_directories(morpheus_authoring PRIVATE
    "${CMAKE_BINARY_DIR}/generated"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/Nuklear"
    "${CMAKE_SOURCE_DIR}/src/agent"
    "${CMAKE_SOURCE_DIR}/src/authoring"
    "${CMAKE_SOURCE_DIR}/src/compiler"
    "${CMAKE_SOURCE_DIR}/src/project"
    "${CMAKE_SOURCE_DIR}/tinycc"
)
if(MORPHEUS_ENABLE_RUNTIME_LEAKCHECK)
    target_include_directories(morpheus_authoring PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/stb"
    )
endif()
target_compile_definitions(morpheus_authoring PRIVATE
    MORPHEUS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    MORPHEUS_EXPORT_TOOL_PATH="${CMAKE_SOURCE_DIR}/tools/morpheus-export"
    MORPHEUS_TCC_LIB_PATH="${MORPHEUS_TCC_BUILD_DIR}"
    MORPHEUS_TCC_INCLUDE_PATH="${MORPHEUS_TCC_SOURCE_DIR}/include"
)
if(MORPHEUS_ENABLE_RUNTIME_LEAKCHECK)
    target_compile_definitions(morpheus_authoring PRIVATE
        MORPHEUS_ENABLE_RUNTIME_LEAKCHECK=1
    )
endif()
target_compile_options(morpheus_authoring PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
target_link_libraries(morpheus_authoring PRIVATE
    morpheus_libtcc
    ${CMAKE_DL_LIBS}
)

add_executable(morpheus MACOSX_BUNDLE
    src/host/main.m
    "${MORPHEUS_ICON_ASSET_CATALOG}"
    "${MORPHEUS_ICON_ICNS}"
)
add_dependencies(morpheus morpheus_tinycc morpheus_app_icon)

target_include_directories(morpheus PRIVATE
    "${CMAKE_BINARY_DIR}/generated"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/Nuklear"
    "${CMAKE_SOURCE_DIR}/Nuklear/demo/metal"
    "${CMAKE_SOURCE_DIR}/src/agent"
    "${CMAKE_SOURCE_DIR}/src/authoring"
    "${CMAKE_SOURCE_DIR}/src/compiler"
    "${CMAKE_SOURCE_DIR}/src/project"
    "${CMAKE_SOURCE_DIR}/src/host"
    "${CMAKE_SOURCE_DIR}/third_party/stb"
    "${CMAKE_SOURCE_DIR}/tinycc"
)

target_compile_definitions(morpheus PRIVATE
    MORPHEUS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    MORPHEUS_AUTHORING_UI_SOURCE_PATH="${CMAKE_SOURCE_DIR}/src/authoring/morpheus_app.c"
    MORPHEUS_AGENT_PROVIDER_PATH="${MORPHEUS_AGENT_PROVIDER}"
    MORPHEUS_OLLAMA_PROVIDER_PATH="${CMAKE_SOURCE_DIR}/tools/morpheus-ollama-agent"
)

target_compile_options(morpheus PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>
    $<$<COMPILE_LANGUAGE:OBJC>:-Wno-c23-extensions>
)

target_link_libraries(morpheus PRIVATE
    morpheus_authoring_ui
    morpheus_authoring
    miniz
    SDL3::SDL3-static
    "-framework AppKit"
    "-framework CoreGraphics"
    "-framework CoreText"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework Foundation"
)
morpheus_force_link_runtime_core(morpheus)

set_target_properties(morpheus PROPERTIES
    ENABLE_EXPORTS TRUE
    OUTPUT_NAME "Morpheus"
    MACOSX_BUNDLE_BUNDLE_NAME "Morpheus"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.morpheus.seed"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/MorpheusInfo.plist.in"
    MACOSX_BUNDLE_ICON_FILE "morpheus"
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
    add_executable(morpheus_nuklear_large_draw_test
        tests/nuklear_large_draw_test.c
    )
    target_include_directories(morpheus_nuklear_large_draw_test PRIVATE
        "${CMAKE_SOURCE_DIR}/Nuklear"
    )
    target_compile_options(morpheus_nuklear_large_draw_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-c23-extensions
    )
    add_test(NAME nuklear_large_draw COMMAND morpheus_nuklear_large_draw_test)

    add_executable(morpheus_nuklear_frame_cache_test
        tests/nuklear_frame_cache_test.c
    )
    target_include_directories(morpheus_nuklear_frame_cache_test PRIVATE
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/host"
    )
    target_compile_options(morpheus_nuklear_frame_cache_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-c23-extensions
    )
    add_test(NAME nuklear_frame_cache COMMAND morpheus_nuklear_frame_cache_test)

    add_executable(morpheus_nuklear_quartz_test
        tests/nuklear_quartz_test.m
    )
    target_include_directories(morpheus_nuklear_quartz_test PRIVATE
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/host"
    )
    target_compile_options(morpheus_nuklear_quartz_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -fobjc-arc
        -Wno-c23-extensions
    )
    target_link_libraries(morpheus_nuklear_quartz_test PRIVATE
        SDL3::SDL3-static
        "-framework AppKit"
        "-framework CoreGraphics"
        "-framework CoreText"
    )
    add_test(NAME nuklear_quartz COMMAND morpheus_nuklear_quartz_test)

    add_executable(morpheus_frame_scheduler_test
        tests/frame_scheduler_test.c
    )
    target_include_directories(morpheus_frame_scheduler_test PRIVATE
        "${CMAKE_SOURCE_DIR}/src/host"
    )
    target_compile_options(morpheus_frame_scheduler_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME frame_scheduler COMMAND morpheus_frame_scheduler_test)

    add_executable(morpheus_capability_registry_test
        tests/capability_registry_test.c
    )
    target_include_directories(morpheus_capability_registry_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
    )
    target_compile_options(morpheus_capability_registry_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME capability_registry COMMAND morpheus_capability_registry_test)

    add_executable(morpheus_authoring_projects_capability_test
        tests/authoring_projects_capability_test.c
    )
    target_include_directories(morpheus_authoring_projects_capability_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/authoring"
        "${CMAKE_SOURCE_DIR}/src/project"
    )
    target_compile_options(morpheus_authoring_projects_capability_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_authoring_projects_capability_test PRIVATE
        morpheus_authoring
    )
    add_test(
        NAME authoring_projects_capability
        COMMAND morpheus_authoring_projects_capability_test)

    add_executable(morpheus_authoring_revisions_capability_test
        tests/authoring_revisions_capability_test.c
    )
    target_include_directories(morpheus_authoring_revisions_capability_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/authoring"
        "${CMAKE_SOURCE_DIR}/src/project"
    )
    target_compile_options(morpheus_authoring_revisions_capability_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_authoring_revisions_capability_test PRIVATE
        morpheus_authoring
    )
    add_test(
        NAME authoring_revisions_capability
        COMMAND morpheus_authoring_revisions_capability_test)

    add_executable(morpheus_authoring_export_capability_test
        tests/authoring_export_capability_test.c
    )
    target_include_directories(morpheus_authoring_export_capability_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/authoring"
    )
    target_compile_options(morpheus_authoring_export_capability_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_authoring_export_capability_test PRIVATE
        morpheus_authoring
    )
    add_test(
        NAME authoring_export_capability
        COMMAND morpheus_authoring_export_capability_test)

    add_executable(morpheus_authoring_app_module_test
        tests/authoring_app_module_test.c
    )
    target_include_directories(morpheus_authoring_app_module_test PRIVATE
        "${CMAKE_BINARY_DIR}/generated"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/agent"
        "${CMAKE_SOURCE_DIR}/src/authoring"
        "${CMAKE_SOURCE_DIR}/src/compiler"
        "${CMAKE_SOURCE_DIR}/src/project"
        "${CMAKE_SOURCE_DIR}/tinycc"
    )
    target_compile_options(morpheus_authoring_app_module_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_compile_definitions(morpheus_authoring_app_module_test PRIVATE
        MORPHEUS_TEST_AUTHORING_UI_SOURCE="${CMAKE_SOURCE_DIR}/src/authoring/morpheus_app.c"
    )
    target_link_libraries(morpheus_authoring_app_module_test PRIVATE
        morpheus_authoring_ui
        morpheus_authoring
    )
    set_target_properties(
        morpheus_authoring_app_module_test PROPERTIES ENABLE_EXPORTS TRUE)
    add_test(
        NAME authoring_app_module
        COMMAND morpheus_authoring_app_module_test)

    add_executable(morpheus_authoring_controller_test
        tests/authoring_controller_test.c
    )
    target_include_directories(morpheus_authoring_controller_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/authoring"
    )
    target_compile_options(morpheus_authoring_controller_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_authoring_controller_test PRIVATE
        morpheus_authoring
    )
    add_test(
        NAME authoring_controller
        COMMAND morpheus_authoring_controller_test)

    add_executable(morpheus_authoring_shell_test
        tests/authoring_shell_test.c
    )
    target_include_directories(morpheus_authoring_shell_test PRIVATE
        "${CMAKE_BINARY_DIR}/generated"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/authoring"
        "${CMAKE_SOURCE_DIR}/src/compiler"
        "${CMAKE_SOURCE_DIR}/tinycc"
    )
    target_compile_definitions(morpheus_authoring_shell_test PRIVATE
        MORPHEUS_TEST_AUTHORING_CANDIDATE="${CMAKE_SOURCE_DIR}/tests/fixtures/module_v1.c"
        MORPHEUS_TEST_INVALID_AUTHORING_CANDIDATE="${CMAKE_SOURCE_DIR}/tests/fixtures/module_compile_error.c"
        MORPHEUS_TEST_INIT_FAILURE_AUTHORING_CANDIDATE="${CMAKE_SOURCE_DIR}/tests/fixtures/module_init_failure.c"
    )
    target_compile_options(morpheus_authoring_shell_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_authoring_shell_test PRIVATE
        morpheus_authoring
    )
    set_target_properties(morpheus_authoring_shell_test PROPERTIES ENABLE_EXPORTS TRUE)
    add_test(NAME authoring_shell COMMAND morpheus_authoring_shell_test)

    add_executable(morpheus_runtime_validation_test
        tests/runtime_validation_test.c
    )
    target_compile_options(morpheus_runtime_validation_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_runtime_validation_test PRIVATE
        morpheus_standalone_runtime
    )
    add_test(NAME runtime_validation COMMAND morpheus_runtime_validation_test)

    if(APPLE AND TARGET morpheus_frozen)
        add_test(
            NAME frozen_isolation
            COMMAND /bin/sh
                "${CMAKE_SOURCE_DIR}/tests/frozen_isolation_test.sh"
                "$<TARGET_FILE:morpheus_frozen>"
                "$<TARGET_BUNDLE_DIR:morpheus_frozen>"
                "${CMAKE_SOURCE_DIR}"
                "${CMAKE_BINARY_DIR}"
        )
    endif()

    add_executable(morpheus_runtime_reload_test
        tests/runtime_reload_test.c
        src/host/json_service.c
    )
    add_dependencies(morpheus_runtime_reload_test morpheus_tinycc)
    target_include_directories(morpheus_runtime_reload_test PRIVATE
        "${CMAKE_BINARY_DIR}/generated"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/authoring"
        "${CMAKE_SOURCE_DIR}/src/compiler"
        "${CMAKE_SOURCE_DIR}/src/project"
        "${CMAKE_SOURCE_DIR}/src/host"
        "${CMAKE_SOURCE_DIR}/tinycc"
    )
    target_compile_definitions(morpheus_runtime_reload_test PRIVATE
        MORPHEUS_TEST_FIXTURE_ROOT="${CMAKE_SOURCE_DIR}/tests/fixtures"
    )
    if(MORPHEUS_ENABLE_RUNTIME_LEAKCHECK)
        target_compile_definitions(morpheus_runtime_reload_test PRIVATE
            MORPHEUS_ENABLE_RUNTIME_LEAKCHECK=1
        )
    endif()
    target_compile_options(morpheus_runtime_reload_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-c23-extensions
    )
    target_link_libraries(morpheus_runtime_reload_test PRIVATE
        morpheus_authoring
        morpheus_runtime_core
        yyjson
    )
    set_target_properties(morpheus_runtime_reload_test PROPERTIES ENABLE_EXPORTS TRUE)
    add_test(NAME runtime_reload COMMAND morpheus_runtime_reload_test)

    if(MORPHEUS_ENABLE_RUNTIME_LEAKCHECK)
        add_executable(morpheus_runtime_leakcheck_test
            tests/runtime_leakcheck_test.c
        )
        target_include_directories(morpheus_runtime_leakcheck_test PRIVATE
            "${CMAKE_SOURCE_DIR}/src/compiler"
        )
        target_compile_options(morpheus_runtime_leakcheck_test PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
        target_link_libraries(morpheus_runtime_leakcheck_test PRIVATE
            morpheus_authoring
        )
        add_test(NAME runtime_leakcheck COMMAND morpheus_runtime_leakcheck_test)
    endif()

    add_executable(morpheus_http_service_test tests/http_service_test.c)
    target_include_directories(morpheus_http_service_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src/host"
    )
    target_compile_options(morpheus_http_service_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>
    )
    target_link_libraries(morpheus_http_service_test PRIVATE morpheus_runtime_core)
    add_test(NAME http_service COMMAND morpheus_http_service_test)

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

    add_executable(morpheus_project_store_test
        tests/project_store_test.c
        src/project/project_store.c
    )
    target_include_directories(morpheus_project_store_test PRIVATE
        "${CMAKE_SOURCE_DIR}/src/project"
    )
    target_compile_options(morpheus_project_store_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    add_test(NAME project_store COMMAND morpheus_project_store_test)

    add_executable(morpheus_sqlite_test tests/sqlite_test.c)
    add_dependencies(morpheus_sqlite_test morpheus_sqlite_external)
    target_compile_options(morpheus_sqlite_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_sqlite_test PRIVATE morpheus_sqlite)
    add_test(NAME sqlite COMMAND morpheus_sqlite_test)

    add_executable(morpheus_math_host_test tests/math_host_test.c)
    target_include_directories(morpheus_math_host_test PRIVATE
        "${CMAKE_SOURCE_DIR}/src/host"
    )
    target_compile_options(morpheus_math_host_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_math_host_test PRIVATE morpheus_math)
    add_test(NAME math_host COMMAND morpheus_math_host_test)

    add_executable(morpheus_la_tinycc_test tests/la_tinycc_test.c)
    add_dependencies(morpheus_la_tinycc_test morpheus_tinycc)
    target_include_directories(morpheus_la_tinycc_test PRIVATE
        "${CMAKE_SOURCE_DIR}/tinycc"
    )
    target_compile_definitions(morpheus_la_tinycc_test PRIVATE
        MORPHEUS_LA_INCLUDE_PATH="${CMAKE_SOURCE_DIR}/third_party/la"
        MORPHEUS_TCC_INCLUDE_PATH="${MORPHEUS_TCC_SOURCE_DIR}/include"
        MORPHEUS_TCC_LIB_PATH="${MORPHEUS_TCC_BUILD_DIR}"
    )
    target_compile_options(morpheus_la_tinycc_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_la_tinycc_test PRIVATE morpheus_libtcc)
    add_test(NAME la_tinycc COMMAND morpheus_la_tinycc_test)

    add_executable(morpheus_database_service_test
        tests/database_service_test.c
    )
    target_include_directories(morpheus_database_service_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/host"
    )
    target_compile_options(morpheus_database_service_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-c23-extensions
    )
    target_link_libraries(morpheus_database_service_test PRIVATE
        morpheus_runtime_core
    )
    add_test(NAME database_service COMMAND morpheus_database_service_test)

    add_executable(morpheus_yyjson_test tests/yyjson_test.c)
    target_include_directories(morpheus_yyjson_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_options(morpheus_yyjson_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_yyjson_test PRIVATE morpheus_runtime_core)
    add_test(NAME yyjson COMMAND morpheus_yyjson_test)

    add_executable(morpheus_miniz_test tests/miniz_test.c)
    target_compile_options(morpheus_miniz_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-function
    )
    target_link_libraries(morpheus_miniz_test PRIVATE miniz)
    add_test(NAME miniz COMMAND morpheus_miniz_test)

    add_executable(morpheus_stb_image_test tests/stb_image_test.c)
    target_include_directories(morpheus_stb_image_test PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/stb"
    )
    target_compile_options(morpheus_stb_image_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_stb_image_test PRIVATE morpheus_stb_image)
    add_test(NAME stb_image COMMAND morpheus_stb_image_test)

    add_executable(morpheus_image_service_test tests/image_service_test.m)
    target_include_directories(morpheus_image_service_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/host"
        "${CMAKE_SOURCE_DIR}/third_party/stb"
    )
    target_compile_options(morpheus_image_service_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -fobjc-arc
        -Wno-c23-extensions
    )
    target_link_libraries(morpheus_image_service_test PRIVATE
        morpheus_runtime_core
    )
    add_test(NAME image_service COMMAND morpheus_image_service_test)
    set_tests_properties(image_service PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 10)

    add_executable(morpheus_image_service_quartz_test
        tests/image_service_quartz_test.m
    )
    target_include_directories(morpheus_image_service_quartz_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/host"
    )
    target_compile_options(morpheus_image_service_quartz_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -fobjc-arc
        -Wno-c23-extensions
    )
    target_link_libraries(morpheus_image_service_quartz_test PRIVATE
        morpheus_runtime_core
    )
    add_test(NAME image_service_quartz
        COMMAND morpheus_image_service_quartz_test)

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
    )
    target_include_directories(morpheus_agent_session_test PRIVATE
        "${CMAKE_BINARY_DIR}/generated"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/agent"
        "${CMAKE_SOURCE_DIR}/src/authoring"
        "${CMAKE_SOURCE_DIR}/src/compiler"
        "${CMAKE_SOURCE_DIR}/src/project"
        "${CMAKE_SOURCE_DIR}/tinycc"
    )
    target_compile_definitions(morpheus_agent_session_test PRIVATE
        MORPHEUS_FAKE_AGENT_PATH="$<TARGET_FILE:morpheus_fake_agent_provider>"
    )
    target_compile_options(morpheus_agent_session_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_agent_session_test PRIVATE
        morpheus_authoring
    )
    add_dependencies(morpheus_agent_session_test morpheus_fake_agent_provider)
    add_test(NAME agent_session COMMAND morpheus_agent_session_test)

    add_executable(morpheus_fake_boundary_agent_provider
        tests/fixtures/fake_boundary_agent_provider.c
    )
    target_compile_options(morpheus_fake_boundary_agent_provider PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    add_executable(morpheus_agent_boundary_test
        tests/agent_boundary_test.c
    )
    target_include_directories(morpheus_agent_boundary_test PRIVATE
        "${CMAKE_BINARY_DIR}/generated"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/agent"
        "${CMAKE_SOURCE_DIR}/src/authoring"
        "${CMAKE_SOURCE_DIR}/src/compiler"
        "${CMAKE_SOURCE_DIR}/src/project"
        "${CMAKE_SOURCE_DIR}/tinycc"
    )
    target_compile_definitions(morpheus_agent_boundary_test PRIVATE
        MORPHEUS_FAKE_BOUNDARY_AGENT_PATH="$<TARGET_FILE:morpheus_fake_boundary_agent_provider>"
    )
    target_compile_options(morpheus_agent_boundary_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_agent_boundary_test PRIVATE
        morpheus_authoring
    )
    add_dependencies(
        morpheus_agent_boundary_test
        morpheus_fake_boundary_agent_provider
    )
    add_test(NAME agent_boundary COMMAND morpheus_agent_boundary_test)

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
        src/host/json_service.c
    )
    add_dependencies(
        morpheus_agent_repair_loop_test
        morpheus_tinycc
        morpheus_fake_repair_agent_provider
    )
    target_include_directories(morpheus_agent_repair_loop_test PRIVATE
        "${CMAKE_BINARY_DIR}/generated"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/agent"
        "${CMAKE_SOURCE_DIR}/src/compiler"
        "${CMAKE_SOURCE_DIR}/tinycc"
    )
    target_compile_definitions(morpheus_agent_repair_loop_test PRIVATE
        MORPHEUS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
        MORPHEUS_TCC_LIB_PATH="${MORPHEUS_TCC_BUILD_DIR}"
        MORPHEUS_TCC_INCLUDE_PATH="${MORPHEUS_TCC_SOURCE_DIR}/include"
        MORPHEUS_REPAIR_AGENT_PATH="$<TARGET_FILE:morpheus_fake_repair_agent_provider>"
        MORPHEUS_VALID_MODULE="${CMAKE_SOURCE_DIR}/tests/fixtures/module_v1.c"
        MORPHEUS_API_HEADER="${CMAKE_SOURCE_DIR}/include/morpheus/app_api.h"
        MORPHEUS_SDK_HEADER="${CMAKE_SOURCE_DIR}/include/morpheus/sdk.h"
    )
    target_compile_options(morpheus_agent_repair_loop_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_agent_repair_loop_test PRIVATE
        morpheus_libtcc
        yyjson
        ${CMAKE_DL_LIBS}
    )
    add_test(NAME agent_repair_loop COMMAND morpheus_agent_repair_loop_test)

    add_test(
        NAME ollama_adapter
        COMMAND /bin/sh
            "${CMAKE_SOURCE_DIR}/tests/ollama_adapter_test.sh"
            "${CMAKE_SOURCE_DIR}/tools/morpheus-ollama-agent"
            "${CMAKE_SOURCE_DIR}/tests/fixtures/fake_ollama_curl"
    )

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
