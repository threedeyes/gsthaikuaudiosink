project(GSTHAIKUAUDIOSINK)

cmake_minimum_required (VERSION 2.24)

find_package (PkgConfig REQUIRED)
pkg_check_modules (GLIB2 REQUIRED glib-2.0>=2.36.0)

include_directories (${GLIB2_INCLUDE_DIRS})
link_directories (${GLIB2_LIBRARY_DIRS})

find_package(LibXml2 REQUIRED)
include_directories(${LIBXML2_INCLUDE_DIR})

add_definitions(-std=gnu++11)

set(GSTHAIKUAUDIO_LIB_NAME gsthaikuaudiosink)

pkg_check_modules(GST1_TEST gstreamer-1.0)
if ( GST1_TEST_FOUND )
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
    pkg_check_modules(GSTREAMER-APP REQUIRED gstreamer-app-1.0)
	set(GSTHAIKUAUDIO_LIBRARIES glib-2.0 gobject-2.0 gstbase-1.0 gstreamer-1.0 gstaudio-1.0 gstcontroller-1.0 intl be root media)
	add_library(${GSTHAIKUAUDIO_LIB_NAME} SHARED src/haikuaudiosink_1.0.cpp)
	include_directories(${GSTREAMER_INCLUDE_DIRS} ${GSTREAMER-APP_INCLUDE_DIRS})
endif()

pkg_check_modules(GST01_TEST gstreamer-0.10)
if ( GST01_TEST_FOUND AND NOT GST1_TEST_FOUND)
	set(GSTHAIKUAUDIO_LIBRARIES glib-2.0 gstreamer-0.10 gstaudio-0.10 be root media)
	add_library(${GSTHAIKUAUDIO_LIB_NAME} SHARED src/haikuaudiosink_0.10.cpp)
endif()

target_link_libraries(${GSTHAIKUAUDIO_LIB_NAME} ${GSTHAIKUAUDIO_LIBRARIES})
