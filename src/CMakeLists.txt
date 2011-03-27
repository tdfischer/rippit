set(rippit_SRCS
	rippit.c
)

include_directories(${GSTREAMER_INCLUDE_DIRS} ${MUSICBRAINZ_INCLUDE_DIRS})

add_executable(rippit ${rippit_SRCS})

target_link_libraries(rippit ${GSTREAMER_LIBRARIES} ${MUSICBRAINZ_LIBRARIES})

install(TARGETS rippit DESTINATION bin)
