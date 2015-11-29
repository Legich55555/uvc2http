cmake_minimum_required(VERSION 2.8)

project(Uvc2Http)

# _GLIBCXX_USE_C99 is defined to use snprintf. 
# -fpermissive is used to allow simple initialization for structures
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_C99 -std=c++11 -static-libstdc++ -fpermissive -Wall -fno-exceptions")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_library(uvc2http_lib STATIC Tracer.cpp StreamFunc.cpp Config.cpp HttpServer.cpp UvcGrabber.cpp MjpegUtils.cpp)

add_executable(uvc2http AppMain.cpp)
target_link_libraries(uvc2http uvc2http_lib)

add_executable(uvc2http_daemon DaemonMain.cpp)
target_link_libraries(uvc2http_daemon uvc2http_lib)

install(TARGETS uvc2http uvc2http_daemon RUNTIME DESTINATION bin)
