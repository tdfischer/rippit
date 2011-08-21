set(rippit_SRCS
	rippit.c
    love.c
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${GSTREAMER_INCLUDE_DIRS} ${MUSICBRAINZ_INCLUDE_DIRS})

add_executable(rippit ${rippit_SRCS})

target_link_libraries(rippit ${GSTREAMER_LIBRARIES} ${MUSICBRAINZ_LIBRARIES})

install(TARGETS rippit DESTINATION bin)
