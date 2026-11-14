#!/bin/sh
# Regenerate the sample clip for the Video app (media/internal + media/external
# in the QEMU images). Deterministic for a given ffmpeg version (testsrc2).
# Pinned ffmpeg version at adoption: 6.1.1 — re-pin if this ever changes.
set -e
OUT="$(dirname "$0")/../external/board/opentv/overlay/media"
mkdir -p "$OUT/internal" "$OUT/external"

# 60 s, 480×270, H.264 baseline + a quiet AAC track (the file is complete;
# playback ignores audio until the audio backend lands — device.conf [audio]).
ffmpeg -y -hide_banner -loglevel error \
	-f lavfi -i "testsrc2=size=480x270:rate=24:duration=60" \
	-f lavfi -i "sine=frequency=440:duration=60" \
	-c:v libx264 -preset veryfast -crf 30 -profile:v baseline -level 3.0 \
	-pix_fmt yuv420p -c:a aac -b:a 32k -shortest -movflags +faststart \
	"$OUT/internal/promo.mp4"

cp "$OUT/internal/promo.mp4" "$OUT/external/promo.mp4"
# durations come from the container now (media.c probes it) — no sidecars
rm -f "$OUT/internal/promo.dur" "$OUT/internal/promo.jpg" \
      "$OUT/external/promo.dur" "$OUT/external/promo.jpg"
ls -la "$OUT/internal/" "$OUT/external/"
