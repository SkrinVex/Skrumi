cmake_minimum_required(VERSION 3.18)
cmake_policy(SET CMP0091 NEW)
set(SKRUMI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
include(cmake/Version.cmake)
project(Skrumi VERSION ${SKRUMI_VERSION} LANGUAGES C CXX)
option(SKRUMI_STATIC_LINUX "Link Linux executables statically (use a musl toolchain for portability)" OFF)
if(SKRUMI_STATIC_LINUX)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "SKRUMI_STATIC_LINUX requires a Linux toolchain")
    endif()
    add_link_options(-static)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

# Standalone programs must not need a separately installed MSVC runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(cmake/EmbedStdlib.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ANDROID)
    if(ANDROID_PLATFORM_LEVEL LESS 28)
        message(FATAL_ERROR "Skrumi Android requires API 28 or newer (Android 9)")
    endif()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Enable compiler warnings
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/EHsc> $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
else()
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra> $<$<COMPILE_LANGUAGE:CXX>:-pedantic>)
endif()

# A Windows thread reserves 1 MB of stack by default where Linux gives 8, so the same
# program could recurse eight times less deeply there. Reserve the same 8 MB.
if(WIN32)
    if(MSVC)
        add_link_options(/STACK:8388608)
    else()
        add_link_options(-Wl,--stack,8388608)
    endif()
endif()

# Reusable Skrumi core library
set(skrumi_core_sources
    src/audio/Mixer.cpp
    src/core/Context.cpp
    src/core/Platform.cpp
    src/core/Network.cpp
    src/core/HttpClient.cpp
    src/core/TlsServer.cpp
    src/core/Runtime.cpp
    src/core/SourceProvider.cpp
    src/core/Unicode.cpp
    src/core/Lexer.cpp
    src/core/Parser.cpp
    src/core/AST.cpp
    src/core/Operations.cpp
    src/core/Compiler.cpp
    src/core/Vm.cpp
    src/core/Project.cpp
    src/core/HttpServer.cpp
    src/core/Process.cpp
    src/core/Template.cpp
    src/core/Formatter.cpp
    src/core/Resolver.cpp
    src/core/Interpreter.cpp
    src/core/SemanticAnalyzer.cpp
    src/core/builtins/Registry.cpp
    src/core/builtins/Core.cpp
    src/core/builtins/Text.cpp
    src/core/builtins/Collections.cpp
    src/core/builtins/System.cpp
    src/core/builtins/Files.cpp
    src/core/builtins/StrictFiles.cpp
    src/core/builtins/Processes.cpp
    src/core/builtins/Directories.cpp
    src/core/builtins/UserRegistry.cpp
    src/core/builtins/UserEnvironment.cpp
    src/core/builtins/NetworkBuiltins.cpp
    src/core/builtins/Sound.cpp
    src/graphics/Font.cpp
    src/graphics/Surface.cpp
    src/graphics/Window.cpp
    src/graphics/Ui.cpp
    src/graphics/Image.cpp
    src/graphics/Builtins.cpp
    src/graphics/Rendering.cpp
)
if(EMSCRIPTEN)
    # The browser playground: no libcurl or Mbed TLS, so no HTTP client or HTTPS server.
    list(REMOVE_ITEM skrumi_core_sources src/core/HttpClient.cpp src/core/TlsServer.cpp)
    list(APPEND skrumi_core_sources src/web/Unavailable.cpp)
endif()
if(ANDROID)
    list(APPEND skrumi_core_sources src/android/Host.cpp)
endif()
add_library(skrumi_core STATIC ${skrumi_core_sources})
target_include_directories(skrumi_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(skrumi_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(skrumi_core PRIVATE SKRUMI_VERSION="${PROJECT_VERSION}")
include(cmake/Fonts.cmake)
include(cmake/Unicode.cmake)
include(cmake/Audio.cmake)
if(EMSCRIPTEN)
    # WebAssembly for the playground page (site/playground): the page loads
    # skrumi.js/skrumi.wasm into a web worker. Nothing else is built.
    target_compile_options(skrumi_core PUBLIC -fwasm-exceptions)
    set(skrumi_web_options -fwasm-exceptions
        -sMODULARIZE=1 -sEXPORT_NAME=createSkrumi -sENVIRONMENT=worker,node
        -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=512MB -sSTACK_SIZE=4MB
        -sEXPORTED_RUNTIME_METHODS=ccall,FS -sEXIT_RUNTIME=0 -sINVOKE_RUN=0)
    add_executable(skrumi_web src/web/Playground.cpp)
    target_link_libraries(skrumi_web PRIVATE skrumi_core)
    set_target_properties(skrumi_web PROPERTIES OUTPUT_NAME skrumi SUFFIX .js)
    target_link_options(skrumi_web PRIVATE ${skrumi_web_options})
    # Programs with a window: every window_poll() and wait() hands the time back to
    # the browser, which delivers the keys and the mouse. Asyncify makes that
    # possible and costs speed and size, so only such programs load this build.
    # Asyncify cannot unwind through WebAssembly exceptions, so this build's core
    # uses Emscripten's JavaScript ones.
    add_library(skrumi_core_graphics STATIC ${skrumi_core_sources})
    target_link_libraries(skrumi_core_graphics PRIVATE freetype utf8proc skrumi_audio)
    target_include_directories(skrumi_core_graphics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                               PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_compile_definitions(skrumi_core_graphics PRIVATE SKRUMI_VERSION="${PROJECT_VERSION}")
    target_compile_options(skrumi_core_graphics PUBLIC -fexceptions)
    add_executable(skrumi_web_graphics src/web/Playground.cpp)
    target_link_libraries(skrumi_web_graphics PRIVATE skrumi_core_graphics)
    set_target_properties(skrumi_web_graphics PROPERTIES OUTPUT_NAME skrumi-graphics SUFFIX .js)
    target_link_options(skrumi_web_graphics PRIVATE -fexceptions
        -sMODULARIZE=1 -sEXPORT_NAME=createSkrumi -sENVIRONMENT=worker,node
        -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=512MB -sSTACK_SIZE=4MB
        -sEXPORTED_RUNTIME_METHODS=ccall,FS -sEXIT_RUNTIME=0 -sINVOKE_RUN=0
        -sASYNCIFY=1 "-sASYNCIFY_IMPORTS=['skrumi_canvas_poll','skrumi_pause']" -sASYNCIFY_STACK_SIZE=1048576)
    return()
endif()
include(cmake/Networking.cmake)
include(cmake/Graphics.cmake)
include(GNUInstallDirs)
if(ANDROID)
    option(SKRUMI_BUILD_ANDROID_JNI "Build the Java/Kotlin Android bridge" ON)
    if(SKRUMI_BUILD_ANDROID_JNI)
        add_library(skrumi_android SHARED src/android/Jni.cpp)
        target_include_directories(skrumi_android PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
        target_link_libraries(skrumi_android PRIVATE skrumi_core log android)
        target_link_options(skrumi_android PRIVATE "-Wl,-z,max-page-size=16384")
        install(TARGETS skrumi_android LIBRARY DESTINATION lib)
    endif()
    install(TARGETS skrumi_core ARCHIVE DESTINATION lib)
    install(DIRECTORY include/skrumi DESTINATION include)
    return()
endif()

if(WIN32)
    # ShellExecuteW opens files with their default application (os_open).
    target_link_libraries(skrumi_core PRIVATE shell32 winmm ole32 advapi32 uuid)
endif()

# Skrumi CLI executable
add_library(skrumi_bundle STATIC
    src/standalone/Bundle.cpp
    src/standalone/Image.cpp
    src/standalone/Build.cpp
)
target_include_directories(skrumi_bundle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/standalone)
target_link_libraries(skrumi_bundle PUBLIC skrumi_core)
# JSON shared by the language server and the debug adapter
add_library(skrumi_json STATIC src/lsp/Json.cpp)
target_include_directories(skrumi_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/lsp)

# Debug Adapter Protocol server inside the skrumi executable (skrumi debug-adapter)
add_library(skrumi_debug STATIC
    src/debug/Channel.cpp
    src/debug/DebugAdapter.cpp
)
target_include_directories(skrumi_debug PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/debug)
target_link_libraries(skrumi_debug PUBLIC skrumi_core skrumi_json)
find_package(Threads REQUIRED)
target_link_libraries(skrumi_debug PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(skrumi_debug PUBLIC ws2_32)
endif()

add_executable(skrumi src/cli/main.cpp src/cli/Launch.cpp src/cli/Help.cpp src/cli/Tools.cpp src/cli/ProjectTools.cpp src/standalone/Stub.cpp)
target_link_libraries(skrumi PRIVATE skrumi_bundle skrumi_debug)
if(WIN32)
    # CommandLineToArgvW: program arguments as UTF-16, converted to UTF-8 for os_args().
    target_link_libraries(skrumi PRIVATE shell32)
endif()
if(MINGW)
    target_link_options(skrumi PRIVATE -static -static-libgcc -static-libstdc++)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(skrumi PRIVATE -static-libgcc -static-libstdc++)
endif()

# The project manifest uses a complete TOML parser, shared with no runtime state.
FetchContent_Declare(tomlplusplus
    URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
    URL_HASH SHA256=8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155)
FetchContent_MakeAvailable(tomlplusplus)
target_link_libraries(skrumi PRIVATE tomlplusplus::tomlplusplus)
target_include_directories(skrumi PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(READ "${tomlplusplus_SOURCE_DIR}/LICENSE" toml_license)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/ProjectLicenses.h" CONTENT
    "#pragma once\ninline const char* projectLicenses() { return R\"SKTOML(toml++ 3.4.0\n${toml_license})SKTOML\"; }\n" @ONLY)

# Skrumi Language Server executable (LSP)
add_executable(skrumi-lsp
    src/lsp/main.cpp
    src/lsp/Transport.cpp
    src/lsp/DocumentManager.cpp
    src/lsp/LspServer.cpp
)
target_include_directories(skrumi-lsp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/lsp)
target_link_libraries(skrumi-lsp PRIVATE skrumi_core skrumi_json)

# CTest integration
# Release builds optimize across source files when linking: calls between the VM,
# the runtime and the builtins become direct and small helpers are inlined.
option(SKRUMI_LTO "Link-time optimization for release builds" ON)
if(SKRUMI_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT skrumi_ipo OUTPUT skrumi_ipo_error LANGUAGES CXX)
    if(skrumi_ipo)
        set_property(TARGET skrumi_core skrumi_bundle skrumi_json skrumi_debug skrumi skrumi-lsp
                     PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        # Functions inlined across files would make every Skrumi call's native frame in
        # the VM loop twice as large, and the recursion a program gets half as deep.
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            set_source_files_properties(src/core/Vm.cpp PROPERTIES COMPILE_OPTIONS "-fno-lto")
        elseif(MSVC)
            set_source_files_properties(src/core/Vm.cpp PROPERTIES COMPILE_OPTIONS "/GL-")
        endif()
    else()
        message(STATUS "Link-time optimization is not available: ${skrumi_ipo_error}")
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation rules
include(GNUInstallDirs)
install(TARGETS skrumi skrumi-lsp skrumi_core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/skrumi DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY std DESTINATION ${CMAKE_INSTALL_DATADIR}/skrumi)
