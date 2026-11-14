#!/bin/sh
# Regenerate the sample clip for the Video app (media/internal + media/external
# in the QEMU images). Deterministic for a given ffmpeg version (testsrc2).
# Pinned ffmpeg version at adoption: 6.1.1 — re-pin if this ever changes.
set -e
OUT="$(dirname "$0")/../external/board/opentv/overlay/media"
mkdir -p "$OUT/internal" "$OUT/external"

# 3 s, 480×270, soft beep, baseline h264 — small and universally decodable.
ffmpeg -y -hide_banner -loglevel error \
	-f lavfi -i "testsrc2=size=480x270:rate=24:duration=3" \
	-f lavfi -i "sine=frequency=440:duration=3" \
	-c:v libx264 -preset veryfast -crf 28 -profile:v baseline -level 3.0 \
	-pix_fmt yuv420p -c:a aac -b:a 48k -shortest -movflags +faststart \
	"$OUT/internal/promo.mp4"

cp "$OUT/internal/promo.mp4" "$OUT/external/promo.mp4"
ls -la "$OUT/internal/promo.mp4" "$OUT/external/promo.mp4"
