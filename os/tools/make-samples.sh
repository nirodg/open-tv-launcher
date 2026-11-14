#!/bin/sh
# Regenerate the sample clip. Deterministic for a given ffmpeg version
# (testsrc2). Pinned ffmpeg version at adoption: 6.1.1 — re-pin if this ever
# changes. The clip is a SEED: the run targets copy it into the persistent
# media volume (/tmp/opentv/media) when that folder is empty.
set -e
OUT="$(dirname "$0")/../external/board/opentv/overlay/usr/share/otv/samples"
mkdir -p "$OUT"

# 60 s, 480×270, H.264 baseline + a quiet AAC track (the file is complete;
# playback ignores audio until the audio backend lands — device.conf [audio]).
ffmpeg -y -hide_banner -loglevel error \
	-f lavfi -i "testsrc2=size=480x270:rate=24:duration=60" \
	-f lavfi -i "sine=frequency=440:duration=60" \
	-c:v libx264 -preset veryfast -crf 30 -profile:v baseline -level 3.0 \
	-pix_fmt yuv420p -c:a aac -b:a 32k -shortest -movflags +faststart \
	"$OUT/promo.mp4"

# durations come from the container (media.c probes it) — no sidecars
rm -f "$OUT/promo.dur" "$OUT/promo.jpg"
ls -la "$OUT/"
