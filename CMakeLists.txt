

cmake_minimum_required(VERSION 3.13)

project(ENYO)

set(CMAKE_CXX_STANDARD 17)

option(ENYO_BUILD_TEST          "Build the enyo test project"   ON)
option(ENYO_FORCE_DEBUG			"Force print messages"			OFF)

if (CMAKE_SYSTEM_NAME STREQUAL Windows)
  list (APPEND ENYO_DEFINITIONS "ENYO_WINDOWS_PLATFORM" "NOMINMAX")
elseif (CMAKE_SYSTEM_NAME STREQUAL Darwin)
  list (APPEND ENYO_DEFINITIONS "ENYO_OSX_PLATFORM")
elseif (CMAKE_SYSTEM_NAME STREQUAL Linux)
  list (APPEND ENYO_DEFINITIONS "ENYO_LINUX_PLATFORM")
  # -rdynamic so we can get backtrace symbols...
  # --no-as-needed so libprofiler sticks around
  string (APPEND CMAKE_EXE_LINKER_FLAGS " -rdynamic -Wl,--no-as-needed")
else ()
  message (SEND_ERROR "Unknown system name: " + CMAKE_SYSTEM_NAME)
endif()

add_library(enyo STATIC src/enyo.cpp src/enyo.h)
target_compile_definitions (enyo PRIVATE ${ENYO_DEFINITIONS} )

if (ENYO_BUILD_TEST)
    add_executable(enyo_test doc/main.cpp)
	target_compile_definitions (enyo_test PRIVATE ${ENYO_DEFINITIONS})
	target_link_libraries(enyo_test enyo)
    target_include_directories(enyo_test PRIVATE src)
    add_test(NAME enyo_test_cmake COMMAND enyo_test)
endif ()