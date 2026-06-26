@echo off
setlocal

set "GSTREAMER_BIN=C:\Software\gstreamer\1.0\msvc_x86_64\bin"

if not exist "%GSTREAMER_BIN%\gst-launch-1.0.exe" (
    echo GStreamer was not found at "%GSTREAMER_BIN%".
    exit /b 1
)

set "PATH=%GSTREAMER_BIN%;%PATH%"

echo Previewing RTP H.264 stream from udp://192.168.0.2:5004
echo Press Ctrl+C to stop preview.

gst-launch-1.0 -v ^
  udpsrc port=5004 caps="application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000" ^
  ! rtpjitterbuffer latency=0 drop-on-latency=true ^
  ! rtph264depay ^
  ! h264parse ^
  ! avdec_h264 max-threads=1 ^
  ! videoconvert ^
  ! autovideosink sync=false

exit /b %ERRORLEVEL%
