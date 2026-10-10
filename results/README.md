# results/

Raw outputs produced on the authors' measurement machine. Every number in
`docs/report.md` must trace back to a file here. Nothing in this folder is typed in or
edited by hand.

| File | Produced by |
|---|---|
| `verify.txt` | `./build/bench --verify > results/verify.txt` |
| `bench_{A,B,C}.txt`, `bench_summary.txt` | `scripts/run_bench.sh` (5 fresh processes per impl) |
| `perf_env.txt` | `scripts/run_perf.sh` (machine, CPU, kernel, perf version, flags) |
| `perf_stat_{baseline,A,B,C}.txt` | `scripts/run_perf.sh` (`perf stat -r 5`) |
| `perf_stat_*_stdout.txt` | the program's own output during those perf stat runs |
| `perf_unsupported.txt` | counters perf reported as `<not supported>` / `<not counted>` |
| `perf_summary.md` | `scripts/perf_summary.sh` (table + IPC / miss-rate formulas, from the raw files) |
| `perf_report_{A,B,C}.txt`, `*_callgraph.txt` | `perf report` on the `perf record -g` samples |
| `perf.data.{A,B,C}` | raw samples, git-ignored (too large); keep them locally |

Status: TODO(human). Empty until the measurement runs are done.
