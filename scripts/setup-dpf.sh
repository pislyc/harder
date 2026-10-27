#!/bin/bash
# Fetch DPF (pinned upstream commit, as a git submodule) and apply HARDER's patches.
# Every patch is behind a define set in plugins/Harder/Makefile (see README.md). Safe to run twice.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
git submodule update --init --recursive dpf
if git -C dpf apply --reverse --check ../patches/dpf-harder.patch 2>/dev/null; then
  echo "DPF patches already applied"
else
  git -C dpf apply ../patches/dpf-harder.patch
  echo "DPF patched"
fi
