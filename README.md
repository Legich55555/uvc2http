# uvc2http
Linux tool for video streaming from USB cameras.

Uvc_streamer is an alternative for mjpg_streamer. It is targeted for small
Linux devices (like routers with OpenWrt).

Requirements:
  * USB video camera with MJPG capture mode support (tested with Logitech
    B910);
  * Linux system with installed driver "uvcvideo" (tested with router 
    TP-Link MR3020 + OpenWrt 15.05);
  * C++11 compiler (tested with GCC 4.8.3).

Usage:
  * Build and run. By default it uses following settings:
      - camera "/dev/video0";
      - capturing mode 640x480x15;
      - listening at TCP port 8081.

Configuration:
  * Put your configuration to file "Config.cpp" and rebuild. CLI options are
    absent because USB cameras have very different options and different 
    behavior on different systems.
    
Expected results:
  * On a router TP-Link MR3020 it produces 15-18 frames at resolution 1280x720.
  * On a PC with 4 core CPU frame rate is limited only by camera restrictions.

Differences from mjpg_streamer
  * Uses only 1 thread;
  * No memory copying;
  * Small memory consumption;
  * Small CPU utilization.


