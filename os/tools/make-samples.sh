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

# duration sidecar + pre-rendered filmstrip (12 fps → 36 frames in a 6×6
# grid): the QEMU Video app "plays" this strip — real decode lands with the
# media backend (no codec deps in the OS image yet).
echo 3 > "$OUT/internal/promo.dur"
ffmpeg -y -hide_banner -loglevel error \
	-i "$OUT/internal/promo.mp4" \
	-vf "fps=12,scale=480:270,tile=6x6" -frames:v 1 -q:v 4 \
	"$OUT/internal/promo.jpg"

cp "$OUT/internal/promo.mp4" "$OUT/external/promo.mp4"
cp "$OUT/internal/promo.dur" "$OUT/external/promo.dur"
cp "$OUT/internal/promo.jpg" "$OUT/external/promo.jpg"
ls -la "$OUT/internal/" "$OUT/external/"
