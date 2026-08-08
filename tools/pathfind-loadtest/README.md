# path_find load harness

Reproduces the concurrent `path_find` load numbers cited in
[PR #7962](https://github.com/XRPLF/rippled/pull/7962): mean update gap near one
ledger close (~4s) with ~100 WebSocket sessions while consensus stays
`FULL` / `load_factor` ≈ 1.

This tree keeps **in-process unit coverage** for the new machinery
(`xrpl.rpc.AssetCache`, `xrpl.rpc.PathFindSub`). Full multi-connection load
testing needs a live node and a client pool, so the harness lives in a small
companion repo and is linked here for review / CI reproducibility.

## Companion harness

|                |                                                                                                                   |
| -------------- | ----------------------------------------------------------------------------------------------------------------- |
| **Repository** | https://github.com/shortthefomo/test-pathfind                                                                     |
| **Modes**      | CLI + Vue dashboard (burst or ramp)                                                                               |
| **Metrics**    | create latency, update-gap time series, `server_info` / `get_counts` (pathfind cache counters), consensus verdict |

### Quick start

```bash
git clone https://github.com/shortthefomo/test-pathfind.git
cd test-pathfind
npm install
npm run discover # cache wallets with funded trust lines → data/wallets.json
npm run cli -- --skipDiscover --mode=ramp --max=100 --observeSec=60 \
    --endpoint=ws://127.0.0.1:6006
```

Dashboard:

```bash
npm run dev # http://localhost:5173
```

Point `--endpoint` at a standalone / pathfinding-capable `xrpld` with admin
RPC if you want `pathfind_cache_*` series from `get_counts`.

### What to look for

- **Consensus**: `server_state` stays `FULL` / `PROPOSING` for the whole hold
- **Update gap**: mean session update interval ≈ ledger close (≤ ~4–5s) at max concurrency
- **Cache**: `pathfind_cache_hits` grows; `pathfind_cache_lines` stable (not thrashing each close)
- **Ramp-down**: lines reclaim toward 0 after the last session closes

### CI note

Unit tests in this PR (`AssetCache`, `PathFindSub`) run in the normal
`xrpld --unittest=…` matrix, including **thread-sanitizer** builds for the
multi-threaded `AssetCache` stress case.

The external harness is intentionally **not** a required GitHub Actions job:
it needs a long-lived funded network (or a heavy local stand-alone with a
wallet cache) and is meant for pre-merge perf validation and operator
benchmarks. Treat a green local CLI run as the evidence pack for the PR
numbers; attach result JSON from `data/results/` when updating the PR.
