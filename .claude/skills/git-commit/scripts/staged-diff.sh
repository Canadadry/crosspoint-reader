#!/usr/bin/env bash
# Prints the staged diff, with rename detection.
#
# Usage: staged-diff.sh [file...]
#   With no args, shows everything staged. With args, scopes to those paths.
set -euo pipefail

git diff --cached -M -- "$@"
