cmake_minimum_required(VERSION 4.3)
project(LogLens C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(LogLens src/main.c)

if(WIN32)
    set_target_properties(LogLens PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
    target_link_libraries(LogLens PRIVATE comdlg32)
endif()
