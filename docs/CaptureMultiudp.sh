#!/usr/bin/env bash

set -euo pipefail

WIDTH="${WIDTH:-1280}"
HEIGHT="${HEIGHT:-720}"
FRAMERATE="${FRAMERATE:-30/1}"
BITRATE_KBIT="${BITRATE_KBIT:-1000}"
KEY_INT_MAX="${KEY_INT_MAX:-30}"
CLIENTS="${CLIENTS:-192.168.0.2:5004}"
CAMERA_NAME="${CAMERA_NAME:-}"
FORCE_SOFTWARE_ENCODER="${FORCE_SOFTWARE_ENCODER:-0}"
USE_V4L2CONVERT="${USE_V4L2CONVERT:-0}"
FORCE_GSTREAMER_CAMERA_SOURCE="${FORCE_GSTREAMER_CAMERA_SOURCE:-0}"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1"
        exit 1
    fi
}

require_command gst-launch-1.0
require_command gst-inspect-1.0

BITRATE_BPS="$((BITRATE_KBIT * 1000))"
RPICAM_FRAMERATE="${RPICAM_FRAMERATE:-${FRAMERATE%/*}}"

if [[ "${FORCE_SOFTWARE_ENCODER}" != "1" ]] && [[ "${FORCE_GSTREAMER_CAMERA_SOURCE}" != "1" ]]; then
    CAMERA_APP=""

    if command -v rpicam-vid >/dev/null 2>&1; then
        CAMERA_APP="rpicam-vid"
    elif command -v libcamera-vid >/dev/null 2>&1; then
        CAMERA_APP="libcamera-vid"
    fi

    if [[ -n "${CAMERA_APP}" ]]; then
        echo "Using Raspberry Pi camera app for capture and H.264 encoding: ${CAMERA_APP}"
        echo "Streaming RTP H.264 video to udp://${CLIENTS}"
        echo "Press Ctrl+C to stop capture."

        CAMERA_APP_COMMAND=(
            "${CAMERA_APP}"
            --timeout 0
            --nopreview
            --flush
            --inline
            --codec h264
            --width "${WIDTH}"
            --height "${HEIGHT}"
            --framerate "${RPICAM_FRAMERATE}"
            --bitrate "${BITRATE_BPS}"
            --intra "${KEY_INT_MAX}"
            -o -
        )

        if [[ -n "${CAMERA_NAME}" ]]; then
            echo "CAMERA_NAME is ignored for ${CAMERA_APP}. Select the camera using the app-specific camera index options if needed."
        fi

        if [[ "${CAMERA_APP}" == "rpicam-vid" ]]; then
            CAMERA_APP_COMMAND+=(--libav-format h264)
        fi

        "${CAMERA_APP_COMMAND[@]}" | gst-launch-1.0 -v \
            fdsrc fd=0 do-timestamp=true \
            ! h264parse config-interval=-1 \
            ! rtph264pay pt=96 config-interval=-1 aggregate-mode=none \
            ! multiudpsink clients="${CLIENTS}" sync=false async=false

        exit 0
    fi
fi

if ! gst-inspect-1.0 libcamerasrc >/dev/null 2>&1; then
    echo "Required GStreamer element not found: libcamerasrc"
    exit 1
fi

SOURCE=(
    libcamerasrc
)

if [[ -n "${CAMERA_NAME}" ]]; then
    SOURCE+=("camera-name=${CAMERA_NAME}")
fi

COMMON_PIPELINE=(
    "${SOURCE[@]}"
    !
    "video/x-raw,width=${WIDTH},height=${HEIGHT},format=NV12,framerate=${FRAMERATE},interlace-mode=progressive"
    !
    queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0
)

if [[ "${FORCE_SOFTWARE_ENCODER}" != "1" ]] && gst-inspect-1.0 v4l2h264enc >/dev/null 2>&1; then
    echo "Using Raspberry Pi hardware H.264 encoder path: libcamerasrc -> v4l2h264enc"

    if [[ "${USE_V4L2CONVERT}" == "1" ]]; then
        if ! gst-inspect-1.0 v4l2convert >/dev/null 2>&1; then
            echo "USE_V4L2CONVERT=1 was requested, but GStreamer element v4l2convert was not found."
            exit 1
        fi

        COMMON_PIPELINE+=(
            !
            v4l2convert
            !
            "video/x-raw,format=NV12"
        )
    fi

    HW_EXTRA_CONTROLS="controls,video_bitrate_mode=0,video_bitrate=${BITRATE_BPS},repeat_sequence_header=1,h264_i_frame_period=${KEY_INT_MAX}"

    echo "Streaming RTP H.264 video to udp://${CLIENTS}"
    echo "Press Ctrl+C to stop capture."

    gst-launch-1.0 -v \
        "${COMMON_PIPELINE[@]}" \
        ! v4l2h264enc extra-controls="${HW_EXTRA_CONTROLS}" \
        ! "video/x-h264,profile=constrained-baseline,stream-format=byte-stream,alignment=au" \
        ! h264parse config-interval=-1 \
        ! rtph264pay pt=96 config-interval=-1 aggregate-mode=none \
        ! multiudpsink clients="${CLIENTS}" sync=false async=false

    exit 0
fi

if ! gst-inspect-1.0 x264enc >/dev/null 2>&1; then
    echo "No suitable H.264 encoder found."
    echo "Checked: v4l2h264enc and x264enc"
    echo "Install package gstreamer1.0-plugins-good for v4l2h264enc or gstreamer1.0-plugins-ugly for x264enc."
    exit 1
fi

echo "Hardware encoder unavailable, falling back to software x264enc."
echo "Streaming RTP H.264 video to udp://${CLIENTS}"
echo "Press Ctrl+C to stop capture."

gst-launch-1.0 -v \
    "${COMMON_PIPELINE[@]}" \
    ! videoconvert \
    ! "video/x-raw,format=I420" \
    ! x264enc tune=zerolatency speed-preset=ultrafast threads=1 bitrate="${BITRATE_KBIT}" key-int-max="${KEY_INT_MAX}" bframes=0 byte-stream=true aud=true \
    ! h264parse config-interval=-1 \
    ! rtph264pay pt=96 config-interval=-1 aggregate-mode=none \
    ! multiudpsink clients="${CLIENTS}" sync=false async=false
