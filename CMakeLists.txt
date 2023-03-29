cmake_minimum_required(VERSION 3.4.1)

project(client)

set(CMAKE_CXX_FLAGS -std=c++17)
set( CMAKE_BUILD_TYPE Release)

#set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

add_definitions("-O0 -Wall -g")

add_library(mythread STATIC thread.c)
add_library(cbuf STATIC cbuf.c thread.c)
add_library(mytimer STATIC timer.cpp timer.h assertions.h queue.h)
target_link_libraries(mytimer 
    rt)
add_executable(client main.cpp)

target_link_libraries(client 
    pthread
    mythread
    mytimer
    cbuf
    folly
    boost_system)