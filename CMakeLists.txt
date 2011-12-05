cmake_minimum_required(VERSION 2.6)
project(rippit)

set(RIPPIT_VERSION_MAJOR 0)
set(RIPPIT_VERSION_MINOR 1)
set(RIPPIT_VERSION_MICRO 0)

find_package(PkgConfig)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-0.10)
pkg_check_modules(MUSICBRAINZ REQUIRED libmusicbrainz3)
pkg_check_modules(DVDNAV REQUIRED dvdnav)

add_subdirectory(src)
