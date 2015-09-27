cmake_minimum_required(VERSION 2.8)

project(uvc_streamer)

# _GLIBCXX_USE_C99 is defined to use snprintf. 
# -fpermissive is used to allow simple initialization for structures
# -static-libstdc++ 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_C99 -std=c++11 -static-libstdc++ -fpermissive -Wall -fno-exceptions")

add_library(uvc_streamer_lib STATIC Tracer.cpp StreamFunc.cpp Config.cpp HttpServer.cpp UvcGrabber.cpp MjpegUtils.cpp)

add_executable(uvc_streamer AppMain.cpp)
target_link_libraries(uvc_streamer uvc_streamer_lib)

add_executable(uvc_streamerd DaemonMain.cpp)
target_link_libraries(uvc_streamerd uvc_streamer_lib)

install(TARGETS uvc_streamer uvc_streamerd RUNTIME DESTINATION bin)
