#!/usr/bin/env bash
# Prints the unstaged diff, with rename detection.
#
# Usage: unstaged-diff.sh [file...]
#   With no args, shows everything unstaged. With args, scopes to those paths.
set -euo pipefail

git diff -M -- "$@"
