#!/bin/bash
# Low-latency GStreamer RTSP pipeline for ZCAM
# Usage: ./zcam-rtsp.sh [camera_ip]

CAMERA_IP="${1:-100.100.100.50}"
RTSP_URL="rtsp://${CAMERA_IP}/live_stream"

echo "Connecting to ZCAM at ${RTSP_URL}..."
echo "Press Ctrl+C to stop"

gst-launch-1.0 -v \
  rtspsrc location="${RTSP_URL}" latency=0 buffer-mode=auto protocols=tcp \
  ! rtph264depay \
  ! h264parse \
  ! avdec_h264 \
  ! videoconvert \
  ! waylandsink sync=false
