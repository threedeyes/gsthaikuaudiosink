project(GSTHAIKUAUDIOSINK)

cmake_minimum_required (VERSION 2.4)

find_package (PkgConfig REQUIRED)
pkg_check_modules (GLIB2 REQUIRED glib-2.0>=2.36.0)

include_directories (${GLIB2_INCLUDE_DIRS})
link_directories (${GLIB2_LIBRARY_DIRS})

find_package(LibXml2 REQUIRED)
include_directories(${LIBXML2_INCLUDE_DIR})

add_definitions(-std=gnu++11)

set(GSTHAIKUAUDIO_LIB_NAME gsthaikuaudiosink)
set(GSTHAIKUAUDIO_LIBRARIES glib-2.0 gstreamer-0.10 gstaudio-0.10 media)
add_library(${GSTHAIKUAUDIO_LIB_NAME} SHARED src/haikuaudiosink.cpp)
target_link_libraries(${GSTHAIKUAUDIO_LIB_NAME} ${GSTHAIKUAUDIO_LIBRARIES})
