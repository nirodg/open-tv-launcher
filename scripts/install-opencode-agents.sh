#!/bin/sh
# install-opencode-agents.sh — copy this repo's agents + skills into OpenCode's
# global config, so they're available in every project/session — not just this
# repo. (Inside this repo, .opencode/ is already auto-discovered; no install
# needed.)
#
# Usage:
#   scripts/install-opencode-agents.sh
#
# Target is $OPENCODE_CONFIG_DIR if set, else ~/.config/opencode (the OpenCode
# V2 global config dir). Re-run to refresh after the repo changes.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC="$ROOT/.opencode"
DEST="${OPENCODE_CONFIG_DIR:-${XDG_CONFIG_HOME:-$HOME/.config}/opencode}"

if [ ! -d "$SRC" ]; then
	echo "error: $SRC not found — run from a checkout of open-tv-launcher" >&2
	exit 1
fi

n=0
mkdir -p "$DEST/agents" "$DEST/skills"

# agents: one <name>.md each
for f in "$SRC"/agents/*.md; do
	[ -e "$f" ] || continue
	cp "$f" "$DEST/agents/"
	echo "agent   $(basename "$f" .md)"
	n=$((n + 1))
done

# skills: each is a directory containing SKILL.md (id = directory name)
for d in "$SRC"/skills/*/; do
	[ -d "$d" ] || continue
	id=$(basename "$d")
	mkdir -p "$DEST/skills/$id"
	cp "$d/SKILL.md" "$DEST/skills/$id/SKILL.md"
	echo "skill   $id"
	n=$((n + 1))
done

echo
echo "Installed $n agent(s)/skill(s) into $DEST"
echo "Open a new OpenCode session to pick them up."
