cmake_minimum_required(VERSION 3.24)

project(morpheus VERSION 0.1.0 LANGUAGES C OBJC)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party libraries statically" FORCE)

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
set(MORPHEUS_SQLITE_LIBRARY "${MORPHEUS_SQLITE_BUILD_DIR}/libsqlite3.a")
file(MAKE_DIRECTORY "${MORPHEUS_SQLITE_BUILD_DIR}")

ExternalProject_Add(morpheus_sqlite_external
    SOURCE_DIR "${MORPHEUS_SQLITE_SOURCE_DIR}"
    BINARY_DIR "${MORPHEUS_SQLITE_BUILD_DIR}"
    DOWNLOAD_COMMAND
        "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_SOURCE_DIR}/third_party/sqlite"
        "${MORPHEUS_SQLITE_SOURCE_DIR}"
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND
        "${CMAKE_COMMAND}" -E env "CC=${CMAKE_C_COMPILER}"
        "${MORPHEUS_SQLITE_SOURCE_DIR}/configure"
        --disable-shared
        --disable-readline
        --disable-load-extension
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

add_library(morpheus_stb_image STATIC src/host/stb_image_impl.c)
target_include_directories(morpheus_stb_image PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/stb"
)
target_compile_options(morpheus_stb_image PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(morpheus MACOSX_BUNDLE
    src/host/main.m
    src/host/http_service.m
    src/host/image_service.m
    src/host/json_service.c
    src/agent/agent_session.c
    src/compiler/runtime_module.c
    src/project/revision_store.c
)
add_dependencies(morpheus morpheus_tinycc)

target_include_directories(morpheus PRIVATE
    "${CMAKE_BINARY_DIR}/generated"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/Nuklear"
    "${CMAKE_SOURCE_DIR}/Nuklear/demo/metal"
    "${CMAKE_SOURCE_DIR}/src/agent"
    "${CMAKE_SOURCE_DIR}/src/compiler"
    "${CMAKE_SOURCE_DIR}/src/project"
    "${CMAKE_SOURCE_DIR}/src/host"
    "${CMAKE_SOURCE_DIR}/third_party/stb"
    "${CMAKE_SOURCE_DIR}/tinycc"
)

target_compile_definitions(morpheus PRIVATE
    MORPHEUS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    MORPHEUS_AGENT_PROVIDER_PATH="${MORPHEUS_AGENT_PROVIDER}"
    MORPHEUS_OLLAMA_PROVIDER_PATH="${CMAKE_SOURCE_DIR}/tools/morpheus-ollama-agent"
    MORPHEUS_TCC_LIB_PATH="${MORPHEUS_TCC_BUILD_DIR}"
    MORPHEUS_TCC_INCLUDE_PATH="${MORPHEUS_TCC_SOURCE_DIR}/include"
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
    morpheus_sqlite
    yyjson
    miniz
    morpheus_stb_image
    SDL3::SDL3-static
    "-framework Metal"
    "-framework QuartzCore"
    "-framework Foundation"
    ${CMAKE_DL_LIBS}
)

set_target_properties(morpheus PROPERTIES
    ENABLE_EXPORTS TRUE
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
        src/host/json_service.c
    )
    add_dependencies(morpheus_runtime_reload_test morpheus_tinycc)
    target_include_directories(morpheus_runtime_reload_test PRIVATE
        "${CMAKE_BINARY_DIR}/generated"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/Nuklear"
        "${CMAKE_SOURCE_DIR}/src/compiler"
        "${CMAKE_SOURCE_DIR}/tinycc"
    )
    target_compile_definitions(morpheus_runtime_reload_test PRIVATE
        MORPHEUS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
        MORPHEUS_TCC_LIB_PATH="${MORPHEUS_TCC_BUILD_DIR}"
        MORPHEUS_TCC_INCLUDE_PATH="${MORPHEUS_TCC_SOURCE_DIR}/include"
        MORPHEUS_TEST_FIXTURE_ROOT="${CMAKE_SOURCE_DIR}/tests/fixtures"
    )
    target_compile_options(morpheus_runtime_reload_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_runtime_reload_test PRIVATE
        morpheus_libtcc
        yyjson
        ${CMAKE_DL_LIBS}
    )
    set_target_properties(morpheus_runtime_reload_test PROPERTIES ENABLE_EXPORTS TRUE)
    add_test(NAME runtime_reload COMMAND morpheus_runtime_reload_test)

    add_executable(morpheus_http_service_test
        tests/http_service_test.c
        src/host/http_service.m
    )
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
    target_link_libraries(morpheus_http_service_test PRIVATE "-framework Foundation")
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

    add_executable(morpheus_sqlite_test tests/sqlite_test.c)
    add_dependencies(morpheus_sqlite_test morpheus_sqlite_external)
    target_compile_options(morpheus_sqlite_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_sqlite_test PRIVATE morpheus_sqlite)
    add_test(NAME sqlite COMMAND morpheus_sqlite_test)

    add_executable(morpheus_yyjson_test
        tests/yyjson_test.c
        src/host/json_service.c
    )
    target_include_directories(morpheus_yyjson_test PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_options(morpheus_yyjson_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(morpheus_yyjson_test PRIVATE yyjson)
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

    add_executable(morpheus_image_service_test
        tests/image_service_test.m
        src/host/image_service.m
        src/host/http_service.m
    )
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
        morpheus_stb_image
        "-framework Foundation"
        "-framework Metal"
    )
    add_test(NAME image_service COMMAND morpheus_image_service_test)
    set_tests_properties(image_service PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 10)

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
