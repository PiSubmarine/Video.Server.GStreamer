@echo off
setlocal

set "GSTREAMER_BIN=C:\Software\gstreamer\1.0\msvc_x86_64\bin"

if not exist "%GSTREAMER_BIN%\gst-launch-1.0.exe" (
    echo GStreamer was not found at "%GSTREAMER_BIN%".
    exit /b 1
)

set "PATH=%GSTREAMER_BIN%;%PATH%"

echo Capturing and streaming RTP H.264 video to udp://127.0.0.1:5004
echo Press Ctrl+C to stop capture.

gst-launch-1.0 -v ^
  mfvideosrc ^
  ! video/x-raw,width=1280,height=720,framerate=30/1 ^
  ! queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 ^
  ! videoconvert ^
  ! video/x-raw,format=NV12 ^
  ! mfh264enc low-latency=true bitrate=1000 ^
  ! h264parse config-interval=-1 ^
  ! rtph264pay pt=96 config-interval=-1 aggregate-mode=none ^
  ! udpsink host=127.0.0.1 port=5004 sync=false async=false

exit /b %ERRORLEVEL%
