cmake_minimum_required(VERSION 2.6)
project(rippit)

find_package(PkgConfig)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-0.10)
pkg_check_modules(MUSICBRAINZ REQUIRED libmusicbrainz3)

add_subdirectory(src)
