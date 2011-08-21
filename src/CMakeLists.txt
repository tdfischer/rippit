set(rippit_SRCS
	rippit.c
    love.c
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${GSTREAMER_INCLUDE_DIRS} ${MUSICBRAINZ_INCLUDE_DIRS})

add_custom_command(OUTPUT rippit.1 COMMAND help2man ${CMAKE_CURRENT_BINARY_DIR}/rippit -o ${CMAKE_CURRENT_BINARY_DIR}/rippit.1 DEPENDS rippit)

add_executable(rippit ${rippit_SRCS} rippit.1)

target_link_libraries(rippit ${GSTREAMER_LIBRARIES} ${MUSICBRAINZ_LIBRARIES})

install(TARGETS rippit DESTINATION bin)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rippit.1 DESTINATION share/man/man1)
