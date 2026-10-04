cmake_minimum_required(VERSION 3.26)
project(TranscriptStudio VERSION 0.1.0 LANGUAGES C CXX)

# Transcript Studio: local transcription + speaker separation + checking.
#   ts_core          engine wrapper, audio, project files, Word import/export
#   ts-cli           command-line front end (tests, batch use)
#   TranscriptStudio the desktop app (C++ + system web view)
#
# The speech engine (NVIDIA NeMo-Speech.cpp) is NOT linked: it is loaded at run time
# from engine/<flavour>/ next to the program, so one package can use CUDA, Vulkan,
# Metal or the CPU depending on the computer. See scripts/ for building the engine.

option(TS_BUILD_APP "Build the desktop app (needs a web view: WebView2 / WebKit)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive-)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

include(FetchContent)
# Single-file libraries, fetched at configure time. SOURCE_SUBDIR points nowhere so
# only the sources are downloaded (their own CMake projects are not added).
FetchContent_Declare(miniaudio
  URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.22.tar.gz
  SOURCE_SUBDIR _none_)
FetchContent_Declare(miniz
  URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
  SOURCE_SUBDIR _none_)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  SOURCE_SUBDIR _none_)
FetchContent_MakeAvailable(miniaudio miniz nlohmann_json)

set(NEMO_SPEECH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/NeMo-Speech.cpp CACHE PATH "NeMo-Speech.cpp source (headers only are used here)")

add_library(ts_core STATIC
  src/engine.cpp
  src/nemo_loader.cpp
  src/audio_decode.cpp
  src/player.cpp
  src/project.cpp
  src/docx.cpp
  src/miniaudio_impl.c
  ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(ts_core PUBLIC
  src
  ${NEMO_SPEECH_DIR}/include
  ${miniaudio_SOURCE_DIR}
  ${miniz_SOURCE_DIR}
  ${nlohmann_json_SOURCE_DIR}/single_include)

if(WIN32)
  target_sources(ts_core PRIVATE src/platform_win.cpp src/audio_decode_win.cpp)
  target_link_libraries(ts_core PUBLIC ole32 shell32 uuid mfplat mfreadwrite mfuuid)
elseif(APPLE)
  enable_language(OBJCXX)
  target_sources(ts_core PRIVATE src/platform_mac.mm src/audio_decode_mac.cpp)
  target_link_libraries(ts_core PUBLIC "-framework AudioToolbox" "-framework CoreFoundation"
                                       "-framework AppKit" "-framework UniformTypeIdentifiers")
else()
  message(FATAL_ERROR "Transcript Studio currently supports Windows and macOS.")
endif()

add_executable(ts-cli src/cli_main.cpp)
target_link_libraries(ts-cli PRIVATE ts_core)

if(TS_BUILD_APP)
  FetchContent_Declare(webview
    GIT_REPOSITORY https://github.com/webview/webview.git
    GIT_TAG 0.12.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(webview)

  # interface files embedded in the program (regenerated when any of them changes)
  set(UI_FILES index.html app.css app.js i18n.js
      fonts/PlayfairDisplay.woff2 fonts/JetBrainsMono.woff2 fonts/Inter.woff2)
  set(UI_DEPS "")
  foreach(f IN LISTS UI_FILES)
    list(APPEND UI_DEPS ${CMAKE_CURRENT_SOURCE_DIR}/ui/${f})
  endforeach()
  list(JOIN UI_FILES "|" UI_FILES_ARG)
  set(UI_BUNDLE ${CMAKE_CURRENT_BINARY_DIR}/ui_bundle.cpp)
  add_custom_command(OUTPUT ${UI_BUNDLE}
    COMMAND ${CMAKE_COMMAND} -DOUT=${UI_BUNDLE} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/ui
            -DFILES=${UI_FILES_ARG} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_ui.cmake
    DEPENDS ${UI_DEPS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_ui.cmake
    COMMENT "Embedding the interface"
    VERBATIM)

  add_executable(TranscriptStudio WIN32 MACOSX_BUNDLE src/app_main.cpp ${UI_BUNDLE})
  target_link_libraries(TranscriptStudio PRIVATE ts_core webview::core)
  if(APPLE)
    set_target_properties(TranscriptStudio PROPERTIES
      MACOSX_BUNDLE_BUNDLE_NAME "Transcript Studio"
      MACOSX_BUNDLE_GUI_IDENTIFIER "org.transcriptstudio.app"
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
  endif()
endif()
