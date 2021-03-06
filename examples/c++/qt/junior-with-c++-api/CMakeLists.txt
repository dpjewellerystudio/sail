cmake_minimum_required(VERSION 3.6.0)

# Enable strict C++11
#
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option to ensable ASAN on supported platforms
#
option(SAIL_ASAN "Enable ASAN with supported compilers." ON)

project(junior-with-c++-api)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if (CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

find_package(PkgConfig REQUIRED)

# Manifest to support Windows 10 is embedded into our RC file
#
set(RC_FILE "")
if (WIN32)
    set(RC_FILE "qtsail.rc")
    set(CMAKE_EXE_LINKER_FLAGS "/MANIFEST:NO")
endif()

add_executable(junior-with-c++-api
                main.cpp
                qtsail.cpp
                qtsail.ui
                "${RC_FILE}")

target_include_directories(junior-with-c++-api PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

# Enable ASAN
#
if (SAIL_ASAN)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(junior-with-c++-api PRIVATE "-fsanitize=address")
        target_link_libraries(junior-with-c++-api "-fsanitize=address")
    else()
        message("ASAN is not supported with this compiler.")
    endif()
endif()

# Add SAIL dependency
#
pkg_check_modules(SAIL_CXX IMPORTED_TARGET REQUIRED libsail-c++)
target_compile_options(junior-with-c++-api     PRIVATE "${SAIL_CXX_CFLAGS}")
target_include_directories(junior-with-c++-api PRIVATE "${SAIL_CXX_INCLUDE_DIRS}")
target_link_libraries(junior-with-c++-api      PkgConfig::SAIL_CXX)

# Add Qt5 dependency
#
find_package(Qt5 COMPONENTS Widgets REQUIRED)
target_link_libraries(junior-with-c++-api Qt5::Widgets)
