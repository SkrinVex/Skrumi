include(FetchContent)
FetchContent_Declare(miniaudio
    URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.23.tar.gz
    URL_HASH SHA256=1062155cc5882b55c48cb37f57a4dc783669e83ae0838535c62b206eeb1587a6)
# Populate only: the upstream default builds examples and extra utilities.
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()
add_library(skrumi_audio STATIC src/audio/Backend.c)
target_include_directories(skrumi_audio PUBLIC ${miniaudio_SOURCE_DIR})
set_target_properties(skrumi_audio PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT EMSCRIPTEN)
    target_link_libraries(skrumi_audio PRIVATE ${CMAKE_DL_LIBS})
endif()
target_link_libraries(skrumi_core PRIVATE skrumi_audio)
file(READ "${miniaudio_SOURCE_DIR}/LICENSE" audio_license)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/licenses/stb_vorbis.LICENSE" vorbis_license)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/AudioLicenses.h" CONTENT
    "#pragma once\ninline const char* skrumiAudioLicense() { return R\"SKAUDIO(miniaudio 0.11.23 (public domain or MIT-0)\n${audio_license}\nstb_vorbis (public domain or MIT), bundled with miniaudio\n${vorbis_license})SKAUDIO\"; }\n" @ONLY)
