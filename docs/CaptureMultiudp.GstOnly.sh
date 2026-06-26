#!/usr/bin/env bash

set -euo pipefail

WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMERATE="${FRAMERATE:-30/1}"
BITRATE_KBIT="${BITRATE_KBIT:-1000}"
KEY_INT_MAX="${KEY_INT_MAX:-30}"
CLIENTS="${CLIENTS:-192.168.0.2:5004}"
CAMERA_NAME="${CAMERA_NAME:-}"
USE_V4L2CONVERT="${USE_V4L2CONVERT:-0}"
EXTRA_SOURCE_CAPS="${EXTRA_SOURCE_CAPS:-}"
EXTRA_ENCODER_CONTROLS="${EXTRA_ENCODER_CONTROLS:-}"
H264_PROFILE="${H264_PROFILE:-constrained-baseline}"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1"
        exit 1
    fi
}

require_command gst-launch-1.0
require_command gst-inspect-1.0

for element in libcamerasrc v4l2h264enc h264parse rtph264pay multiudpsink; do
    if ! gst-inspect-1.0 "${element}" >/dev/null 2>&1; then
        echo "Required GStreamer element not found: ${element}"
        exit 1
    fi
done

SOURCE=(
    libcamerasrc
)

if [[ -n "${CAMERA_NAME}" ]]; then
    SOURCE+=("camera-name=${CAMERA_NAME}")
fi

BITRATE_BPS="$((BITRATE_KBIT * 1000))"

SOURCE_CAPS="video/x-raw,width=${WIDTH},height=${HEIGHT},format=NV12,framerate=${FRAMERATE},interlace-mode=progressive"
if [[ -n "${EXTRA_SOURCE_CAPS}" ]]; then
    SOURCE_CAPS+=",${EXTRA_SOURCE_CAPS}"
fi

ENCODER_CONTROLS="controls,repeat_sequence_header=1,video_bitrate_mode=0,video_bitrate=${BITRATE_BPS},h264_i_frame_period=${KEY_INT_MAX}"
if [[ -n "${EXTRA_ENCODER_CONTROLS}" ]]; then
    ENCODER_CONTROLS+=",${EXTRA_ENCODER_CONTROLS}"
fi

PIPELINE=(
    "${SOURCE[@]}"
    !
    "${SOURCE_CAPS}"
    !
    queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0
)

if [[ "${USE_V4L2CONVERT}" == "1" ]]; then
    if ! gst-inspect-1.0 v4l2convert >/dev/null 2>&1; then
        echo "USE_V4L2CONVERT=1 was requested, but GStreamer element v4l2convert was not found."
        exit 1
    fi

    PIPELINE+=(
        !
        v4l2convert
        !
        "video/x-raw,format=NV12"
    )
fi

echo "Using GStreamer-only Raspberry Pi pipeline: libcamerasrc -> v4l2h264enc"
echo "Target board profile: Raspberry Pi Zero 2 W"
echo "Streaming RTP H.264 video to udp://${CLIENTS}"
echo "Source caps: ${SOURCE_CAPS}"
echo "Encoder controls: ${ENCODER_CONTROLS}"
echo "Press Ctrl+C to stop capture."
echo
echo "If this fails on Pi Zero 2 W, try one of:"
echo "  BITRATE_KBIT=600 WIDTH=960 HEIGHT=540 ./CaptureMultiudp.GstOnly.sh"
echo "  USE_V4L2CONVERT=1 ./CaptureMultiudp.GstOnly.sh"
echo "  FRAMERATE=25/1 ./CaptureMultiudp.GstOnly.sh"

gst-launch-1.0 -v \
    "${PIPELINE[@]}" \
    ! v4l2h264enc extra-controls="${ENCODER_CONTROLS}" \
    ! "video/x-h264,profile=${H264_PROFILE},stream-format=byte-stream,alignment=au" \
    ! h264parse config-interval=-1 \
    ! rtph264pay pt=96 config-interval=-1 aggregate-mode=none \
    ! multiudpsink clients="${CLIENTS}" sync=false async=false
