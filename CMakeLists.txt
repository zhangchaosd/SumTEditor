cmake_minimum_required(VERSION 3.20)

project(SumTEditor VERSION 0.1.0 LANGUAGES CXX)

option(SUMT_BUILD_TUI "Build the interactive FTXUI editor" ON)
option(SUMT_USE_SYSTEM_FTXUI "Use a system-provided FTXUI package" OFF)
set(SUMT_FTXUI_ARCHIVE "${PROJECT_SOURCE_DIR}/FTXUI-main.zip" CACHE FILEPATH
  "Local FTXUI source archive used before falling back to GitHub")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sumt_core STATIC
  src/config.cpp
  src/document.cpp
)
target_include_directories(sumt_core PUBLIC
  ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(sumt_core PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(sumt_core PRIVATE /W4)
else()
  target_compile_options(sumt_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SUMT_BUILD_TUI)
  if(SUMT_USE_SYSTEM_FTXUI)
    find_package(ftxui CONFIG REQUIRED)
  else()
    include(FetchContent)
    if(EXISTS "${SUMT_FTXUI_ARCHIVE}")
      message(STATUS "Using local FTXUI archive: ${SUMT_FTXUI_ARCHIVE}")
      FetchContent_Declare(ftxui
        URL "${SUMT_FTXUI_ARCHIVE}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      )
    else()
      FetchContent_Declare(ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
        GIT_TAG main
      )
    endif()
    FetchContent_MakeAvailable(ftxui)
  endif()

  add_executable(sumteditor
    src/main.cpp
    src/tui_app.cpp
  )
  target_link_libraries(sumteditor PRIVATE
    sumt_core
    ftxui::component
    ftxui::dom
    ftxui::screen
  )

  if(MSVC)
    target_compile_options(sumteditor PRIVATE /W4)
  else()
    target_compile_options(sumteditor PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

enable_testing()

add_executable(sumt_core_tests
  tests/test_core.cpp
)
target_link_libraries(sumt_core_tests PRIVATE sumt_core)
add_test(NAME sumt_core_tests COMMAND sumt_core_tests)

if(TARGET sumteditor)
  add_test(NAME sumteditor_help COMMAND sumteditor --help)
endif()
