#!/usr/bin/env bash
# run_bench.sh — benchmark A, B and C with every run in a FRESH process.
#
#   scripts/run_bench.sh            5 processes per impl (default)
#   scripts/run_bench.sh 7          7 processes per impl
#
# Why separate processes: runs inside one process share heap state. A and B allocate and
# free hundreds of thousands of nodes per run, so later in-process runs can be affected by
# earlier ones. Each process here runs `bench --impl X --runs 1`, and the script reports
# the median of the per-process numbers.
#
# Raw output goes to results/bench_{A,B,C}.txt (one block per process) and the medians to
# results/bench_summary.txt. These files are for the authors to keep: numbers count only
# if they were produced on the machine the report describes.
set -euo pipefail

cd "$(dirname "$0")/.."
runs="${1:-5}"
out="${RESULTS_DIR:-results}"  # override to keep trial runs out of results/
bench=./build/bench
[[ -x "$bench" || -x "$bench.exe" ]] || { echo "build first: scripts/build.sh" >&2; exit 1; }

mkdir -p "$out"
summary="$out/bench_summary.txt"
{
    echo "# bench summary: $runs fresh processes per impl, median of each metric"
    echo "# host: $(uname -srm)  date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# $("$bench" --impl A --runs 1 --total-only --ops 1000 | sed -n 's/^Build: /build: /p')"
} > "$summary"

# median of the numbers on stdin
median() { sort -g | awk '{ v[NR] = $1 } END { if (NR % 2) print v[(NR + 1) / 2]; else print (v[NR / 2] + v[NR / 2 + 1]) / 2 }'; }

for impl in A B C; do
    raw="$out/bench_${impl}.txt"
    : > "$raw"
    for ((r = 1; r <= runs; r++)); do
        echo "=== process $r/$runs ===" >> "$raw"
        "$bench" --impl "$impl" --runs 1 >> "$raw"
    done

    # Each process prints "Run 1: total X ms | ns/add A ns/cancel B ns/bbo C ns/depth D".
    total=$(sed -n 's/^Run 1: total *\([0-9.]*\) ms.*/\1/p' "$raw" | median)
    metric() { sed -n "/^Run 1:/s/.*ns\/$1 *\([-0-9.]*\).*/\1/p" "$raw" | median; }
    checksums=$(sed -n 's/^Checksum: //p' "$raw" | sort -u | tr '\n' ' ')

    printf 'Impl %s: total %s ms | ns/add %s | ns/cancel %s | ns/bbo %s | ns/depth %s | checksum(s) %s\n' \
        "$impl" "$total" "$(metric add)" "$(metric cancel)" "$(metric bbo)" "$(metric depth)" "$checksums" \
        | tee -a "$summary"
done

echo "Raw output: $out/bench_{A,B,C}.txt   Summary: $summary"
echo "Every impl must show the same single checksum."
