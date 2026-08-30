find_program(NVIM_EXECUTABLE nvim)
if(NVIM_EXECUTABLE)
  add_test(
    NAME lua.headless
    COMMAND ${NVIM_EXECUTABLE} --headless -u
      ${CMAKE_CURRENT_SOURCE_DIR}/minimal_init.lua
      -l ${CMAKE_CURRENT_SOURCE_DIR}/lua/run.lua
  )
  set_tests_properties(lua.headless PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 20
  )
endif()
