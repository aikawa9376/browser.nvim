cmake_minimum_required(VERSION 3.21)
project(browser.nvim LANGUAGES C CXX)

include(CTest)
add_subdirectory(browserd)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
