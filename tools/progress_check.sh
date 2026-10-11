#!/bin/sh
# usage: progress_check.sh --install
#        progress_check.sh <prepare-commit-msg arguments>
# Git prepare-commit-msg hook guarding progress.md, the checklist plan-pilot
# keeps at the root. --install writes the hook into the repository's hooks
# directory (shared by every worktree). Runs even under --no-verify.
# A commit is refused unless, against the base (HEAD, or HEAD^ for --amend):
# the file is a nested `- [ ]`/`- [x]` list indented by 2 spaces; every base
# line is kept with its text and order; no new line comes before a base [x];
# no [x] goes back to [ ]; at least one more [x]. Creating the file is free;
# deleting it needs every base item [x]. Without the file, nothing is checked.

set -u

if [ "${1:-}" = "--install" ]; then
    hooks=$(git rev-parse --git-common-dir)/hooks
    mkdir -p "$hooks"
    cat >"$hooks/prepare-commit-msg" <<'EOF'
#!/bin/sh
exec "$(git rev-parse --show-toplevel)/tools/progress_check.sh" "$@"
EOF
    chmod +x "$hooks/prepare-commit-msg"
    exit 0
fi

source_kind=${2:-}
source_commit=${3:-}
case $source_kind in
    merge|squash) exit 0 ;;
esac
base=HEAD
if [ "$source_kind" = "commit" ] && [ "$(git rev-parse "$source_commit" 2>/dev/null)" = "$(git rev-parse HEAD 2>/dev/null)" ]; then
    base=HEAD^
fi

old=$(mktemp)
new=$(mktemp)
trap 'rm -f "$old" "$new"' EXIT

has_old=1
has_new=1
git show "$base:progress.md" >"$old" 2>/dev/null || has_old=0
git show ":progress.md" >"$new" 2>/dev/null || has_new=0

fail() {
    echo "progress.md: $*" >&2
    echo "progress.md: commit refused (see tools/progress_check.sh)" >&2
    exit 1
}

if [ $has_old -eq 0 ] && [ $has_new -eq 0 ]; then
    exit 0
fi

if [ $has_new -eq 0 ]; then
    if grep -q -- '- \[ \]' "$old"; then
        fail "deleted while items are still [ ]"
    fi
    exit 0
fi

[ $has_old -eq 1 ] || : >"$old"

awk -v is_creation=$((1 - has_old)) '
function key(line) { sub(/- \[x\] /, "- [ ] ", line); return line }
function done(line) { return line ~ /^ *- \[x\] / }
function fail(message) { print "progress.md: " message > "/dev/stderr"; failed = 1; exit 1 }
FILENAME == ARGV[1] {
    if ($0 ~ /^[ \t]*$/) next
    old_count++; old_key[old_count] = key($0); old_done[old_count] = done($0); old_text[old_count] = $0
    next
}
{
    if ($0 ~ /^[ \t]*$/) next
    if ($0 !~ /^( {2})*- \[[ x]\] [^ ]/) fail("line " FNR " is not a \"- [ ] \" or \"- [x] \" item indented by 2 spaces: " $0)
    match($0, /^ */); depth = RLENGTH / 2
    if (depth > previous_depth + 1) fail("line " FNR " is indented more than one level below the previous item: " $0)
    previous_depth = depth
    new_count++; new_key[new_count] = key($0); new_done[new_count] = done($0)
}
END {
    if (failed) exit 1
    j = 1; last_done_position = 0; ticks = 0
    for (i = 1; i <= new_count; i++) {
        if (j <= old_count && new_key[i] == old_key[j]) {
            if (old_done[j] && !new_done[i]) fail("an [x] went back to [ ]: " old_text[j])
            if (!old_done[j] && new_done[i]) ticks++
            if (old_done[j]) last_done_position = i
            j++
        } else {
            is_new[i] = 1
            if (new_done[i]) ticks++
        }
    }
    if (j <= old_count) fail("a line was removed, renamed or moved: " old_text[j])
    for (i = 1; i < last_done_position; i++)
        if (is_new[i]) fail("a new line was added before an existing [x]")
    if (!is_creation && ticks == 0) fail("the commit ticks no new [x]")
}
' "$old" "$new" || fail "invalid change"
