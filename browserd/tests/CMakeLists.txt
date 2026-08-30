add_executable(browserd_json_test json_test.cc)
target_link_libraries(browserd_json_test PRIVATE browserd_core)
add_test(NAME browserd.json COMMAND browserd_json_test)

add_executable(browserd_kitty_test kitty_renderer_test.cc)
target_link_libraries(browserd_kitty_test PRIVATE browserd_core)
add_test(NAME browserd.kitty_renderer COMMAND browserd_kitty_test)

add_executable(browserd_manager_test browser_manager_test.cc)
target_link_libraries(browserd_manager_test PRIVATE browserd_core)
add_test(NAME browserd.manager COMMAND browserd_manager_test)

add_executable(browserd_framebuffer_test framebuffer_test.cc)
target_link_libraries(browserd_framebuffer_test PRIVATE browserd_common)
add_test(NAME browserd.framebuffer COMMAND browserd_framebuffer_test)

add_test(
  NAME browserd.protocol
  COMMAND ${CMAKE_COMMAND}
    -DBROWSERD=$<TARGET_FILE:browserd>
    -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/protocol.jsonl
    -P ${CMAKE_CURRENT_SOURCE_DIR}/protocol_test.cmake
)
