cmake_minimum_required(VERSION 2.8)

project(uvc_streamer)

# _GLIBCXX_USE_C99 is defined to use snprintf.
# -fpermissive is used to allow simple initialization for structures
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_C99 -std=c++11 -fpermissive -static-libstdc++ -Wall -fno-exceptions")

add_executable(uvc_streamer Main.cpp Config.cpp HttpServer.cpp UvcGrabber.cpp MjpegUtils.cpp)

install(TARGETS uvc_streamer RUNTIME DESTINATION bin)
