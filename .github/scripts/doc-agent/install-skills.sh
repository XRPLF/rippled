#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

SRC_DIR="$REPO_ROOT/docs/skills"
DEST_DIR="$REPO_ROOT/.claude/skills"

if [ ! -d "$SRC_DIR" ]; then
  echo "Source directory not found: $SRC_DIR" >&2
  exit 1
fi

mkdir -p "$DEST_DIR"

shopt -s nullglob
moved=0
for src in "$SRC_DIR"/*.md; do
  name="$(basename "$src" .md)"
  [ "$name" = "index" ] && continue

  skill_dir="$DEST_DIR/$name"
  mkdir -p "$skill_dir"
  cp "$src" "$skill_dir/SKILL.md"
  echo "Installed: $name -> $skill_dir/SKILL.md"
  moved=$((moved + 1))
done

echo "Done. Installed $moved skill(s) to $DEST_DIR"
