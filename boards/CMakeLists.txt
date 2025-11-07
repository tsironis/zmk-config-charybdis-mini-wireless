cmake_minimum_required(VERSION 3.20)

if(CONFIG_CHARYBDIS)
  zephyr_library_named(charybdis)
  
  # Add shield source files if any
  # zephyr_library_sources(boards/shields/charybdis/charybdis.c)
  
  # Include shield directories
  target_include_directories(charybdis PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/shields
  )
endif()