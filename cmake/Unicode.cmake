# Unicode 16 extended grapheme clusters (UAX #29), pinned for reproducible builds.
include(FetchContent)
set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(utf8proc
    URL https://github.com/JuliaStrings/utf8proc/archive/refs/tags/v2.10.0.tar.gz
    URL_HASH SHA256=6f4f1b639daa6dca9f80bc5db1233e9cbaa31a67790887106160b33ef743f136)
FetchContent_MakeAvailable(utf8proc)
set_target_properties(utf8proc PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(skrumi_core PRIVATE utf8proc)
file(READ "${utf8proc_SOURCE_DIR}/LICENSE.md" unicode_license)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/UnicodeLicenses.h" CONTENT
    "#pragma once\ninline const char* skrumiUnicodeLicense() { return R\"SKUNICODE(utf8proc 2.10.0\n${unicode_license})SKUNICODE\"; }\n" @ONLY)
