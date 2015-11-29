# uvc2http
A Linux tool for video streaming from USB cameras.

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
  * Put your configuration to file "Config.cpp" and rebuild. A few parameters
    can be specified via CLI:
      --device DEVICE   camera device name
      --buffers NUMBER  capture buffers number
      --width WIDTH     frame width
      --height HEIGHT   frame height
      --fps FPS         capture fps
      --port PORT       HTTP server port
    For example: uvc2http --device /dev/video0 --buffers 4 --width 1280 --height 720 --fps 30 --port 8080
  * Note:
    - Real capture framerate can depend on lighting conditions (exposition settings).
    - Streameing can influence framerate on clients.
    
Expected results:
  * On a router TP-Link MR3020 it produces up to 20 frames at resolution 1280x720.
  * On a PC with 4 core CPU frame rate is limited only by camera restrictions.

Differences from mjpg_streamer
  * Uses only 1 thread;
  * No memory copying;
  * Small memory consumption;
  * Small CPU utilization.


