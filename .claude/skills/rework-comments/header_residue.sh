#!/bin/sh
# Prints every comment left in the given C headers that the bar forbids:
# anything but a comment containing TODO.
# Exit 1 if any is printed. A header that documents an external file format
# is passed with -x and skipped.
# Usage: header_residue.sh [-x allowed.h]... file.h...
skip=""
while [ "$1" = "-x" ]; do skip="$skip $2"; shift 2; done
status=0
for f in "$@"; do
    case " $skip " in *" $f "*) continue ;; esac
    awk -v file="$f" '
        function flush() {
            if (text !~ /TODO/) {
                printf "%s:%d: %s\n", file, start, substr(text, 1, 100)
                bad = 1
            }
            text = ""
        }
        {
            line = $0
            while (line != "") {
                if (in_block) {
                    end = index(line, "*/")
                    if (end == 0) { text = text " " line; line = "" }
                    else { text = text " " substr(line, 1, end + 1); line = substr(line, end + 2); in_block = 0; flush() }
                } else {
                    b = index(line, "/*"); l = index(line, "//")
                    if (b == 0 && l == 0) break
                    if (l != 0 && (b == 0 || l < b)) { start = NR; text = substr(line, l); flush(); break }
                    start = NR; in_block = 1; text = ""; line = substr(line, b)
                    end = index(substr(line, 3), "*/")
                    if (end != 0) { text = substr(line, 1, end + 3); line = substr(line, end + 4); in_block = 0; flush() }
                    else { text = line; line = "" }
                }
            }
        }
        END { exit bad }
    ' "$f" || status=1
done
exit $status
