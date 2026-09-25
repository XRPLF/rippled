# OpenTelemetry Integration Testing Guide

This document describes how to verify the xrpld OpenTelemetry telemetry
pipeline end-to-end, from span generation through the observability stack
(otel-collector, Tempo, Prometheus, Grafana).

---

## Prerequisites

### Build xrpld with telemetry

Build as [BUILD.md](../../BUILD.md) **§ Steps** describes, adding
`-o telemetry=True` to the `conan install` line. That is the only change:
Conan carries `telemetry=ON` into the generated CMake toolchain, so no extra
CMake flag is needed. For the full telemetry build, including how to turn it
off, see [`docs/build/telemetry.md`](../../docs/build/telemetry.md).

This document assumes the `.build/` layout, so the binary is at `.build/xrpld`
and every command below runs from the repo root.

### Required tools

- **Docker** with `docker compose` (v2)
- **curl**
- **jq** (JSON processor)

### Verify binary

```bash
.build/xrpld --version
```

---

## Test 1: Single-Node Standalone (Quick Verification)

This test verifies RPC and transaction spans in standalone mode, plus the
consensus spans that a simulated round still produces. The proposal, voting
and peer-facing consensus spans do not fire — see the expected-spans table at
the end of this test for which do and which do not.

### Step 1: Start the observability stack

```bash
docker compose -f docker/telemetry/docker-compose.yml up -d
```

The `xrpld-logdir-init` service creates `docker/telemetry/data/logs` and gives it
to uid/gid 1000. If `id -u` on this host is not 1000, xrpld cannot write its log
there and the log pipeline stays empty, so set the ids first:

```bash
XRPLD_UID=$(id -u) XRPLD_GID=$(id -g) \
    docker compose -f docker/telemetry/docker-compose.yml up -d
```

Wait for services to be ready:

```bash
# otel-collector readiness: the health_check extension answers on 13133, which
# docker-compose.yml publishes.
curl -sf http://localhost:13133/ >/dev/null && echo "collector ready"

# Tempo readiness
curl -sf http://localhost:3200/ready >/dev/null && echo "tempo ready"
```

### Step 2: Start xrpld in standalone mode

`xrpld-telemetry.cfg` is a Devnet config whose `[node_db]`, `[database_path]` and `[debug_logfile]` all resolve under `docker/telemetry/data`. Standalone builds its own private chain, so pointing it at that store leaves one NuDB holding two unrelated chains. This is the same rule stated for the key-generation node in Test 2, and the reason the sibling mainnet config keeps its store under `data/mainnet/`. Give standalone its own prefix:

```bash
sed -e 's|^path=docker/telemetry/data/nudb$|path=docker/telemetry/data/standalone/nudb|' \
    -e 's|^docker/telemetry/data$|docker/telemetry/data/standalone|' \
    -e 's|^data/logs/xrpld-devnet/debug.log$|data/logs/xrpld-standalone/debug.log|' \
    docker/telemetry/xrpld-telemetry.cfg >/tmp/xrpld-standalone.cfg

.build/xrpld --conf /tmp/xrpld-standalone.cfg -a --start
```

Wait a few seconds for the node to initialize.

> Separating the store is required whether or not `--start` is passed. `--start` selects `StartUpType::Fresh`, but the default `Normal` reaches `startGenesisLedger()` through the same branch chain in `ApplicationImp::setup`, so every standalone run writes a genesis ledger into whichever store the config names. Dropping the flag does not avoid it; only a separate path does. `--start` additionally seeds the amendments this build desires into that genesis ledger.

### Step 3: Exercise RPC spans

```bash
# server_info
curl -s http://localhost:5005 \
    -d '{"method":"server_info"}' | jq .result.info.server_state

# server_state
curl -s http://localhost:5005 \
    -d '{"method":"server_state"}' | jq .result.state.server_state

# ledger
curl -s http://localhost:5005 \
    -d '{"method":"ledger","params":[{"ledger_index":"current"}]}' |
    jq .result.ledger_current_index
```

### Step 4: Submit a transaction

Close the ledger to drive a simulated consensus round — that round is what
produces the `consensus.*` spans. It is not required for `submit` itself:
standalone puts the node in `OperatingMode::FULL` at startup
(`NetworkOPsImp::setStandAlone()`), and the one validated-ledger-age gate on
the submit path is skipped when `config.standalone()` is set
(`checkTxJsonFields()` in `src/xrpld/rpc/detail/TransactionSign.cpp`).

```bash
curl -s http://localhost:5005 -d '{"method":"ledger_accept"}'
```

Submit a Payment from the genesis account:

```bash
curl -s http://localhost:5005 -d '{
  "method": "submit",
  "params": [{
    "secret": "snoPBrXtMeMyMHUVTgbuqAfg1SUTb",
    "tx_json": {
      "TransactionType": "Payment",
      "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
      "Destination": "rN7n7otQDd6FczFgLdSqtcsAUxDkw6fzRH",
      "Amount": "10000000"
    }
  }]
}' | jq .result.engine_result
```

Expected result: `"tesSUCCESS"`.

The destination does not have to exist yet. 10 XRP is exactly the default base
reserve (`FeeSetup::accountReserve` in `src/xrpld/core/Config.h`), so the
payment creates and funds the account. `integration-test.sh` does not hardcode
a destination at all — it calls `wallet_propose` and uses the `account_id` that
comes back.

Close the ledger again to finalize:

```bash
curl -s http://localhost:5005 -d '{"method":"ledger_accept"}'
```

### Step 5: Verify traces in Tempo

Wait 5 seconds for the batch export, then see the "Verification Queries" section below. Its span loop is a superset of what standalone mode produces, so compare its output against the "Expected spans (standalone mode)" table above rather than running a second, narrower set of queries here.

Or open Grafana Explore with Tempo datasource: http://localhost:3000

### Step 6: Teardown

```bash
# Kill xrpld (Ctrl+C or)
pkill -f 'xrpld --conf docker/telemetry/xrpld-telemetry\.cfg'

# Stop observability stack
docker compose -f docker/telemetry/docker-compose.yml down

# Clean xrpld data
rm -rf docker/telemetry/data/
```

The pattern is anchored on the whole `--conf <path>` argument with the `.`
escaped, so it matches this node and not another xrpld run or an editor whose
command line happens to name the same file. `pkill` is also a no-op when
nothing matches, where `kill $(pgrep ...)` errors out with no arguments.

### Expected spans (standalone mode)

| Span Name                                                                     | Expected | Notes                                      |
| ----------------------------------------------------------------------------- | -------- | ------------------------------------------ |
| `rpc.http_request`                                                            | Yes      | Every HTTP RPC call                        |
| `rpc.process`                                                                 | Yes      | Every RPC processing                       |
| `rpc.command.server_info`                                                     | Yes      | server_info RPC                            |
| `rpc.command.server_state`                                                    | Yes      | server_state RPC                           |
| `rpc.command.ledger`                                                          | Yes      | ledger RPC                                 |
| `rpc.command.submit`                                                          | Yes      | submit RPC                                 |
| `rpc.command.ledger_accept`                                                   | Yes      | ledger_accept RPC                          |
| `rpc.ws_upgrade`, `rpc.ws_message`                                            | No       | Need a WebSocket client                    |
| `tx.process`                                                                  | Yes      | Transaction submission                     |
| `tx.preflight`, `tx.preclaim`, `tx.transactor`                                | Yes      | Apply stages of the Payment                |
| `tx.apply`                                                                    | Yes      | Ledger build applies the tx set            |
| `tx.receive`                                                                  | No       | No peers in standalone                     |
| `txq.enqueue`, `txq.apply_direct`                                             | Yes      | `TxQ::apply` on the submit path            |
| `txq.accept`, `txq.cleanup`                                                   | Yes      | Run on every ledger close                  |
| `txq.accept_tx`, `txq.batch_clear`                                            | No       | Nothing is ever queued here                |
| `ledger.build`, `ledger.store`                                                | Yes      | `buildLCL` builds, then stores             |
| `ledger.validate`                                                             | No       | `checkAccept` is unreachable in standalone |
| `consensus.round`, `.phase.open`, `.ledger_close`, `.accept`, `.accept.apply` | Yes      | `ledger_accept` drives a simulated round   |
| `consensus.mode_change`                                                       | Yes      | Fires once per round start                 |
| `consensus.establish`, `.update_positions`, `.check`                          | No       | `phaseEstablish()` never runs              |
| `consensus.proposal.send`, `.validation.send`                                 | No       | The config carries no validator key        |
| `consensus.proposal.receive`, `.validation.receive`                           | No       | No peers                                   |
| `peer.proposal.receive`, `peer.validation.receive`                            | No       | No peers                                   |
| `pathfind.*`                                                                  | No       | No path request, no path subscription      |
| `grpc.*`                                                                      | No       | No `[port_grpc]` in the config             |

Four of the "No" rows have a reason worth spelling out.

- `ledger.validate` belongs to `LedgerMaster::checkAccept`, and standalone never
  reaches it: `consensusBuilt` returns early when standalone, and `switchLCL`
  takes its standalone branch instead of calling `checkAccept`. That
  `getNeededValidations()` returns 0 in standalone is therefore not enough on its
  own.
- `consensus.establish`, `.update_positions` and `.check` are started from
  `phaseEstablish()`. `simulate` does call `closeLedger({})` — which is exactly
  why `.phase.open` and `.ledger_close` do fire — and then sets the phase to
  `Accepted` itself, so `phaseEstablish()` is never entered.
- `.proposal.send` and `.validation.send` are absent for a different reason
  again: `xrpld-telemetry.cfg` carries no `validation_seed` or
  `validator_token`, so `preStartRound` leaves `validating_` false. The node
  observes rather than proposes, and `validate()` — the owner of
  `.validation.send` — is never called.
- `pathfind.update_all` is emitted only while at least one path subscription is
  active, and this test makes no `path_find` or `ripple_path_find` call.

`.mode_change` is in the "Yes" rows because it does not depend on the mode
actually changing. `startRoundInternal` calls `mode_.set()`, `MonitoredMode::set`
calls `onModeChange` with no equality test, and `onModeChange` creates the span
before the `before != after` check — that check guards only the censorship-detector
reset.

One `consensus.round` span reaches Tempo, not two. `roundSpan_` is reset only at
the top of the next `startRoundTracing()`, so after the two `ledger_accept` calls
the first round's span has ended and been exported while the second is still open.
Only ended spans are exported.

---

## Test 2: 6-Node Consensus Network (Full Verification)

This test verifies ALL span categories including consensus and peer
transaction relay, using a 6-node validator network.

### Automated

Run the integration test script:

```bash
bash docker/telemetry/integration-test.sh
```

It checks prerequisites, clears the previous run, brings up the observability stack, generates six validator key pairs and their node configs, starts the nodes, waits for consensus and then for a validated ledger, exercises RPC and submits a transaction, verifies traces in Tempo and both the span_metrics and the native `beast::insight` metrics that arrive over OTLP in Prometheus, checks that no StatsD listener is needed, then prints a summary and leaves the stack running.

The authoritative sequence is the 14 `# Step N:` banner comments in the script source, so read the file rather than the console — none of the script's 48 runtime `log` lines print a step number. The sequence is not restated here, because a numbered copy of it drifts as soon as a step is added.

Its Tempo checks cover the RPC, transaction, consensus, ledger and peer span categories from a fixed list, which is narrower than the loop in the "Verification Queries" section below.

### Manual

If you prefer to run the steps manually:

#### Step 1: Start observability stack

```bash
XRPLD_LOG_DIR=/tmp/xrpld-integration \
    docker compose -f docker/telemetry/docker-compose.yml up -d
```

The override is required here. The collector's log mount defaults to the
repo-relative `docker/telemetry/data/logs`, but this test writes its logs under
`/tmp/xrpld-integration`, so without it the `file_log` receiver tails the wrong
root, no log line reaches Loki, and Test 3 Step 3 finds nothing with no error.

#### Step 2: Generate validator keys

Give the throwaway node a config of its own, under the same temp root the rest
of this test uses:

```bash
mkdir -p /tmp/xrpld-integration/temp-keygen
cat >/tmp/xrpld-integration/temp-keygen/xrpld.cfg <<'EOCFG'
[server]
port_rpc_temp

[port_rpc_temp]
port = 5099
ip = 127.0.0.1
admin = 127.0.0.1
protocol = http

[node_db]
type=NuDB
path=/tmp/xrpld-integration/temp-keygen/nudb
online_delete=256

[database_path]
/tmp/xrpld-integration/temp-keygen/db

[debug_logfile]
/tmp/xrpld-integration/temp-keygen/debug.log

[ssl_verify]
0
EOCFG
```

Do not point this node at `docker/telemetry/xrpld-telemetry.cfg`. That is a
Devnet config whose `[node_db]`, `[database_path]` and `[debug_logfile]` all
resolve under `docker/telemetry/data`, so `--start` (a fresh-genesis start)
would write a genesis chain into the Devnet store, and deleting that directory
afterwards would also destroy the sibling mainnet node's store and every log
under `data/logs/`. Its RPC port is 5005, which is node 1's port later in this
test.

Start it and wait for RPC before asking for keys:

```bash
.build/xrpld --conf /tmp/xrpld-integration/temp-keygen/xrpld.cfg -a --start &
TEMP_PID=$!
until curl -sf http://localhost:5099 -d '{"method":"server_info"}' >/dev/null; do
    sleep 1
done
```

Generate 6 key pairs:

```bash
for i in $(seq 1 6); do
    curl -s http://localhost:5099 \
        -d '{"method":"validation_create"}' | jq '.result'
done
```

Record the `validation_seed` and `validation_public_key` for each.
Stop the temporary node and remove only its own directory:

```bash
kill $TEMP_PID
wait $TEMP_PID 2>/dev/null
rm -rf /tmp/xrpld-integration/temp-keygen
```

#### Step 3: Create node configs

For each node (1-6), create a config file. Template:

```ini
[server]
port_rpc
port_peer

[port_rpc]
port = {5004 + node_number}
ip = 127.0.0.1
admin = 127.0.0.1
protocol = http

[port_peer]
port = {51234 + node_number}
ip = 0.0.0.0
protocol = peer

[network_id]
1025

[node_db]
type=NuDB
path=/tmp/xrpld-integration/Node-{N}/nudb
online_delete=256

[database_path]
/tmp/xrpld-integration/Node-{N}/db

[debug_logfile]
/tmp/xrpld-integration/Node-{N}/debug.log

[validation_seed]
{seed from step 2}

[validators_file]
/tmp/xrpld-integration/validators.txt

[ips_fixed]
{one "127.0.0.1 <port>" line for each port in 51235-51240 except this node's
own 51234 + node_number — a node must not list itself as a fixed peer, so
each config carries five lines, not six}

[peer_private]
1

[telemetry]
enabled=1
service_instance_id=Node-{N}
traces_endpoint=http://localhost:4318/v1/traces
metrics_endpoint=http://localhost:4318/v1/metrics
batch_size=512
batch_delay_ms=2000
max_queue_size=2048
trace_rpc=1
trace_transactions=1
trace_consensus=1
trace_peer=1
trace_ledger=1

[insight]
# server=otel is the only load-bearing key here -- it selects OTelCollector.
# The export endpoint comes from [telemetry] metrics_endpoint, and [insight]'s
# own service_instance_id/service_name keys are ignored.
server=otel

[rpc_startup]
{ "command": "log_level", "severity": "info" }

[ssl_verify]
0
```

`[network_id]` has to be a private id (anything other than 0, 1 or 2), because
the config default is id 0 and the telemetry resource maps that to `mainnet` —
without the stanza every span and metric this local cluster emits is stamped
`xrpl.network.type=mainnet` and lands on the same dashboard series as real
mainnet data. Only 0, 1 and 2 have names, so a private id is stamped
`xrpl.network.type=unknown`. That is the value to select in the dashboards'
Network Type filter when looking at this cluster.

The per-node directory name must equal `[telemetry] service_instance_id`: the
collector reads the node name off the log file's path and stamps it as the Loki
label `service_instance_id`, so a mismatch leaves the logs labelled with a node
name that no trace or metric shares.

`log_level` is `info`, not `warning`. A log line carries trace context only when
it is emitted inside an active span, and the pair that reliably carries it — the
`CNF Val` / `CNF buildLCL` branches inside the consensus accept span, one of
which fires for every accepted ledger — logs at `info`.

#### Step 4: Create validators.txt

```ini
[validators]
{public_key_1}
{public_key_2}
{public_key_3}
{public_key_4}
{public_key_5}
{public_key_6}
```

#### Step 5: Start all 6 nodes

```bash
for i in $(seq 1 6); do
    .build/xrpld --conf /tmp/xrpld-integration/node$i/xrpld.cfg --start &
    echo $! >/tmp/xrpld-integration/node$i/xrpld.pid
done
```

#### Step 6: Wait for consensus

Poll each node until `server_state` = `"proposing"`:

```bash
for port in 5005 5006 5007 5008 5009 5010; do
    while true; do
        state=$(curl -s http://localhost:$port \
            -d '{"method":"server_info"}' |
            jq -r '.result.info.server_state')
        echo "Port $port: $state"
        [ "$state" = "proposing" ] && break
        sleep 5
    done
done
```

#### Step 7: Exercise RPC and submit transaction

```bash
# RPC calls
curl -s http://localhost:5005 -d '{"method":"server_info"}'
curl -s http://localhost:5005 -d '{"method":"server_state"}'
curl -s http://localhost:5005 -d '{"method":"ledger","params":[{"ledger_index":"current"}]}'

# Submit transaction
curl -s http://localhost:5005 -d '{
  "method": "submit",
  "params": [{
    "secret": "snoPBrXtMeMyMHUVTgbuqAfg1SUTb",
    "tx_json": {
      "TransactionType": "Payment",
      "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
      "Destination": "rN7n7otQDd6FczFgLdSqtcsAUxDkw6fzRH",
      "Amount": "10000000"
    }
  }]
}' | jq .result.engine_result
```

Expected result: `"tesSUCCESS"`, the same as Test 1 Step 4.

Wait 15 seconds for the consensus round and the trace batch export. Prometheus
needs longer: `integration-test.sh` waits a further 20 s before its span_metrics
queries and another 20 s before its StatsD queries, so 35 s and 55 s after the
submit. Querying the metrics block at 15 s returns no series, which looks like a
broken pipeline and is not one.

#### Step 8: Verify in Tempo and Prometheus

See the "Verification Queries" section below.

---

## Expected Span Catalog

What follows is a **trigger** catalogue, not an attribute reference: one row per
span-name family, saying which config toggle gates it and what you have to do to
make it appear. It covers all 41 span-name families the code emits, in eight
subsystem groups — RPC (5), gRPC (1), Transaction (6), TxQ (6), Consensus (13),
Ledger (4), Peer (2), PathFind (4).

For each span's **attributes** — span name, source file, full attribute set and
description, per subsystem — see
[`docs/telemetry-runbook.md`](../../docs/telemetry-runbook.md) **§ Span
Reference**; its **§ Protocol Span Flow** gives the parent/child shape of a trace
and calls out where telemetry parenting deliberately differs from the protocol
flow. Both are kept in step with the code, so they are the reference to trust.
One hole worth knowing: the runbook's Span Reference tables have no row for
`grpc.<MethodName>` (it appears only in Protocol Span Flow). Its attributes are
`method`, `grpc_role` and `grpc_status`, emitted from `GRPCServer.cpp` with the
key constants in `src/xrpld/app/main/GrpcSpanNames.h`.

### Span → How to Trigger

"Test" is the section of this file that exercises the family. `T1` = Test 1
(standalone), `T2` = Test 2 (6-node network).

| Span family (count)                                                                                                             | Config toggle        | How to trigger                                                                                                                                                                                                                                                     | Test    |
| ------------------------------------------------------------------------------------------------------------------------------- | -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------- |
| **RPC** (5 total, 3 here): `rpc.http_request`, `rpc.process`, `rpc.command.<name>`                                              | `trace_rpc=1`        | Any HTTP JSON-RPC call: `curl -s http://localhost:5005 -d '{"method":"server_info"}'`. `rpc.command.<name>` is one family — the command name is part of the span name.                                                                                             | T1      |
| **RPC** (cont.): `rpc.ws_message`, `rpc.ws_upgrade`                                                                             | `trace_rpc=1`        | Needs a WebSocket client against `[port_ws_public]` (**6005**) or `[port_ws_admin_local]` (6006). `rpc.ws_upgrade` covers the handshake — force a failure to see its error path. `curl` alone will not do it.                                                      | —       |
| **gRPC** (1): `grpc.<MethodName>`                                                                                               | `trace_rpc=1`        | Call a gRPC method (`GetLedger`, `GetLedgerData`, …). **Requires a `[port_grpc]` stanza — the shipped `xrpld-telemetry*.cfg` files define none**, so add one first.                                                                                                | —       |
| **Transaction** (6 total, 4 here): `tx.process`, `tx.preflight`, `tx.preclaim`, `tx.transactor`                                 | `trace_transactions` | Submit any transaction (T1 Step 4). The three apply-stage spans share the tx's deterministic trace id; the `stage` attribute says where a failing tx stopped.                                                                                                      | T1      |
| **Transaction** (cont.): `tx.receive`                                                                                           | `trace_transactions` | A **peer** relays a transaction. Never appears in standalone — submit on one node of the cluster and look on another.                                                                                                                                              | T2      |
| **Transaction** (cont.): `tx.apply`                                                                                             | `trace_transactions` | Ledger close with a non-empty transaction set: submit, then `ledger_accept` (T1) or wait for consensus (T2).                                                                                                                                                       | T1 / T2 |
| **TxQ** (6): `txq.enqueue`, `txq.apply_direct`, `txq.batch_clear`, `txq.accept`, `txq.accept_tx`, `txq.cleanup`                 | `trace_transactions` | `txq.enqueue`/`apply_direct` on every submission; `txq.accept`/`accept_tx`/`cleanup` on every ledger close. To force real queueing, submit faster than ledgers close or with a fee below the required fee level.                                                   | T1      |
| **Consensus** (13 total, 6 here): `consensus.round`, `.phase.open`, `.mode_change`, `.ledger_close`, `.accept`, `.accept.apply` | `trace_consensus=1`  | A standalone `ledger_accept` drives a whole simulated round, so these six fire in T1 as well as on every real close in T2. Note `consensus.round` is ended by the **next** round's start, so a single `ledger_accept` leaves it open and Tempo will not return it. | T1 / T2 |
| **Consensus** (cont., 3): `.establish`, `.update_positions`, `.check`                                                           | `trace_consensus=1`  | Need the establish phase, which the simulated round skips by jumping straight to `Accepted`. Bring up T2 and wait for a timer-driven round.                                                                                                                        | T2      |
| **Consensus** (cont., 2): `.proposal.send`, `.validation.send`                                                                  | `trace_consensus=1`  | Need the node to propose, which needs a **validator key** — not peers. The shipped `xrpld-telemetry*.cfg` set no `[validation_seed]`/`[validator_token]`, so a standalone node only observes.                                                                      | T2      |
| **Consensus** (cont., 2): `.proposal.receive`, `.validation.receive`                                                            | `trace_consensus=1`  | A peer's consensus message arriving. T2 only.                                                                                                                                                                                                                      | T2      |
| **Ledger** (4 total, 2 here): `ledger.build`, `ledger.store`                                                                    | `trace_ledger=1`     | Any ledger close: `ledger_accept` in standalone, or consensus in T2.                                                                                                                                                                                               | T1 / T2 |
| **Ledger** (cont.): `ledger.validate`                                                                                           | `trace_ledger=1`     | Belongs to `LedgerMaster::checkAccept`, which standalone never reaches — `consensusBuilt` returns early and `switchLCL` takes its standalone branch instead. Needs peers or an inbound validation.                                                                 | T2      |
| **Ledger** (cont.): `ledger.acquire`                                                                                            | `trace_ledger=1`     | Node fetches a **missing** ledger from peers. Start a node with no history against a running cluster, or restart one node after the others have advanced.                                                                                                          | T2      |
| **Peer** (2): `peer.proposal.receive`, `peer.validation.receive`                                                                | `trace_peer=1`       | Inbound consensus messages from peers; fresh trace roots. T2 only, and high volume.                                                                                                                                                                                | T2      |
| **PathFind** (4): `pathfind.request`, `pathfind.compute`, `pathfind.discover`, `pathfind.update_all`                            | `trace_rpc=1`        | `curl -s http://localhost:5005 -d '{"method":"ripple_path_find","params":[{"source_account":"…","destination_account":"…","destination_amount":"100"}]}'`. `pathfind.update_all` fires on ledger close while a request is active.                                  | T1      |

Notes that matter when a span you expect is missing:

- **Toggles are per-subsystem and all default to on** (`trace_rpc`,
  `trace_transactions`, `trace_consensus`, `trace_peer`, `trace_ledger`), but
  `[telemetry] enabled` defaults to **0** — nothing is emitted until it is `1`.
- **`peer.*` cannot be produced in standalone mode.** Both peer spans are created
  in inbound message handlers, and `-a` turns peerfinder's `autoConnect` off, so
  the node opens no outbound peer connections and receives nothing. If Test 1
  shows none, that is correct behaviour, not a regression.
- **`consensus.*` is only partly absent in standalone.** `consensus.round`,
  `.phase.open`, `.mode_change`, `.ledger_close`, `.accept` and `.accept.apply`
  all fire on a `ledger_accept`; the other seven need the establish phase, a
  validator key, or a peer — see the Consensus rows above.
- **`rpc.ws_*` and `grpc.*` need a client and a port the quick tests do not
  use.** Absence in T1/T2 is expected.
- Trace ids are deterministic for transactions (from `txID`) and consensus rounds
  (from `prevLedgerHash`): the trace id is the hash's first **16 bytes**, so from
  a hex-printed hash take the first **32 characters**. This holds under the
  default `consensus_trace_strategy=deterministic`; set it to `random` and each
  node gives its round a random trace id instead, joinable only by the
  `consensus_ledger_id` attribute.

---

## Verification Queries

### Tempo API

Base URL: `http://localhost:3200`

Run `RUN_START=$(date +%s)` **before** starting xrpld (Test 1 Step 2, Test 2
Step 5), in the same shell you will run the block below in. Tempo keeps blocks
for `block_retention` (`tempo.yaml`, 1h) on a named volume, so a search with no
time bound is answered by the previous run's traces.

```bash
TEMPO="http://localhost:3200"

# Refuse to run unbounded rather than report a previous run's traces.
: "${RUN_START:?record RUN_START=\$(date +%s) before starting xrpld}"

# List all services
curl -s "$TEMPO/api/v2/search/tag/resource.service.name/values" | jq '.tagValues[].value'

# Count traces per span name. Test 1 produces a subset of this list — read it
# against the "Expected spans (standalone mode)" table above, not as pass/fail.
#
# -G is required: it moves the urlencoded parameters into the query string.
# Without it curl POSTs them as a request body, Tempo answers 200 and ignores
# the query, and every span name comes back non-zero. start/end bound the
# search to this run; the end margin covers spans exported while the query is
# in flight.
for op in "rpc.http_request" "rpc.process" \
    "rpc.command.server_info" "rpc.command.server_state" "rpc.command.ledger" \
    "rpc.command.submit" "rpc.command.ledger_accept" \
    "tx.process" "tx.receive" "tx.apply" \
    "tx.preflight" "tx.preclaim" "tx.transactor" \
    "txq.enqueue" "txq.apply_direct" "txq.accept" "txq.cleanup" \
    "consensus.round" "consensus.phase.open" "consensus.ledger_close" \
    "consensus.establish" "consensus.update_positions" "consensus.check" \
    "consensus.accept" "consensus.accept.apply" \
    "consensus.proposal.send" "consensus.validation.send" \
    "consensus.mode_change" \
    "consensus.proposal.receive" "consensus.validation.receive" \
    "ledger.build" "ledger.validate" "ledger.store" \
    "peer.proposal.receive" "peer.validation.receive"; do
    count=$(curl -sfG "$TEMPO/api/search" \
        --data-urlencode "q={resource.service.name=\"xrpld\" && name=\"$op\"}" \
        --data-urlencode "start=$RUN_START" \
        --data-urlencode "end=$(($(date +%s) + 60))" \
        --data-urlencode "limit=5" |
        jq '.traces | length')
    printf "%-35s %s traces\n" "$op" "$count"
done
```

Eight more span families exist but need a trigger neither test performs, so they
are counted separately — a zero here is the expected answer, not a failure.
`rpc.ws_*` need a WebSocket client, the `pathfind.*` family needs a `path_find`
or `ripple_path_find` call, and the two `txq` names need a transaction sitting in
the queue.

```bash
for op in "rpc.ws_upgrade" "rpc.ws_message" \
    "pathfind.request" "pathfind.compute" "pathfind.discover" "pathfind.update_all" \
    "txq.accept_tx" "txq.batch_clear"; do
    count=$(curl -sfG "$TEMPO/api/search" \
        --data-urlencode "q={resource.service.name=\"xrpld\" && name=\"$op\"}" \
        --data-urlencode "start=$RUN_START" \
        --data-urlencode "end=$(($(date +%s) + 60))" \
        --data-urlencode "limit=5" |
        jq '.traces | length')
    printf "%-35s %s traces\n" "$op" "$count"
done
```

The remaining family is `grpc.<method>`, whose span name is the gRPC method, so
it has no fixed string to query and needs a `[port_grpc]` stanza neither test
configures.

### Prometheus API

Base URL: `http://localhost:9090`

```bash
PROM="http://localhost:9090"

# Span call counts (from the span_metrics connector). The span_ prefix is the
# connector's `namespace: "span"` in otel-collector-config.yaml; drop that
# setting and these become traces_span_metrics_*.
curl -s "$PROM/api/v1/query?query=span_calls_total" |
    jq '.data.result[] | {span: .metric.span_name, count: .value[1]}'

# Latency histogram
curl -s "$PROM/api/v1/query?query=span_duration_milliseconds_count" |
    jq '.data.result[] | {span: .metric.span_name, count: .value[1]}'

# RPC calls by command
curl -s "$PROM/api/v1/query?query=span_calls_total{span_name=~\"rpc.command.*\"}" |
    jq '.data.result[] | {command: .metric["command"], count: .value[1]}'

# Deployment-tier labels present on metrics (set by the collector's
# resource/tier processor and promoted via resource_to_telemetry_conversion).
# Expect deployment_environment and xrpl_network_type on each series.
curl -s "$PROM/api/v1/query?query=span_calls_total" |
    jq '.data.result[0].metric | {deployment_environment, xrpl_network_type, service_name}'
```

### Grafana

Open http://localhost:3000 (anonymous admin access enabled).

Pre-configured dashboards: every `.json` under
`docker/telemetry/grafana/dashboards/` is provisioned into the `xrpld` folder —
`provisioning/dashboards/dashboards.yaml` points the file provider at
`/var/lib/grafana/dashboards`, which `docker-compose.yml` bind-mounts from that
directory. Adding a file there is all that is needed; there is no per-dashboard
registration.

For what each dashboard covers, see
[`docs/telemetry-runbook.md`](../../docs/telemetry-runbook.md) **§ Grafana
Dashboards**. That reference is partial: 9 of the 15 provisioned dashboards have
a section there, and six — `fee-market`, `job-queue`, `ledger-data-sync`,
`overlay-traffic-detail`, `peer-quality` and `validator-health` — do not. For
those, open a panel's info icon in Grafana; the panel descriptions carry the same
reference format.

Pre-configured datasources:

- **Tempo**: Trace data at `http://tempo:3200`
- **Prometheus**: Metrics at `http://prometheus:9090`
- **Loki**: Log data at `http://loki:3100` (via Grafana Explore)

---

## Exporting to Grafana Cloud

Instead of (or alongside) the local backends, the collector can forward
traces, metrics, and logs to a hosted **Grafana Cloud** stack. This is a
runtime choice layered on top of the base stack — xrpld and the base
`docker-compose.yml` are unchanged.

### Step 1: Get Grafana Cloud OTLP credentials

From **Grafana Cloud → Connections → OpenTelemetry (OTLP)**, note the OTLP
gateway endpoint (ends in `/otlp`), the numeric instance id, and an
access-policy token with `metrics:write`, `traces:write`, and `logs:write`.

### Step 2: Fill in the env file

```bash
cp docker/telemetry/.env.grafanacloud.example docker/telemetry/.env.grafanacloud
# edit .env.grafanacloud:
#   GRAFANA_CLOUD_OTLP_ENDPOINT=https://otlp-gateway-<zone>.grafana.net/otlp
#   GRAFANA_CLOUD_INSTANCE_ID=<instance id>
#   GRAFANA_CLOUD_API_TOKEN=<token>
```

`.env.grafanacloud` is gitignored — never commit real tokens.

### Step 3: Start the stack with cloud export enabled

```bash
docker compose -f docker/telemetry/docker-compose.yml \
    -f docker/telemetry/docker-compose.grafanacloud.yaml up -d
```

The override swaps the collector onto `otel-collector-config.grafanacloud.yaml`.
It keeps the local Tempo/Prometheus/Loki exporters and adds an
`otlphttp/grafanacloud` exporter, but it is **not** the base config plus one
exporter — it restructures the pipelines. Bring the stack up with just the base
file to return to local-only.

Differences that change what you will see:

|                     | Base (`otel-collector-config.yaml`) | Cloud override                                                                                      |
| ------------------- | ----------------------------------- | --------------------------------------------------------------------------------------------------- |
| Pipelines           | 3: `traces`, `metrics`, `logs`      | 5: `traces/metrics`, `traces/store`, `metrics/local`, `metrics/cloud`, `logs`                       |
| Trace sampling      | none — 100% of spans reach Tempo    | `tail_sampling` keeps **0.5%** (one `probabilistic` policy, `decision_wait: 10s`) on `traces/store` |
| `debug` exporter    | present on `traces`                 | dropped                                                                                             |
| Cloud metric labels | n/a                                 | `transform/cloudlabels` on `metrics/cloud` only                                                     |

Consequences worth knowing before you debug against the cloud stack:

- **Traces are sampled, span metrics are not.** Sampling sits only on
  `traces/store` (the pipeline feeding Tempo _and_ Grafana Cloud). The
  `spanmetrics` connector is fed by the separate, unsampled `traces/metrics`
  pipeline, so `span_*` rates stay exact while only ~1 trace in 200 is
  retrievable by trace ID. A trace you can see in a metric may not exist in
  Tempo.
- **Account addresses read the same on both configs.** Neither config hashes
  or drops the `pathfind_*_account` or `tx_*` account attributes: an address is
  a public ledger identifier and is stored as the node emitted it, so a trace
  from the base stack joins a trace from the cloud stack by account.

### Step 4: Verify data reaches Grafana Cloud

After exercising RPC/transaction workflows (Tests 1 or 2), open your Grafana
Cloud instance and confirm:

- **Traces**: Explore → hosted Tempo datasource → search `{resource.service.name="xrpld"}`
- **Metrics**: Explore → hosted Prometheus/Mimir → query `span_calls_total`
- **Logs**: Explore → hosted Loki → query `{service_name="xrpld"}` (requires file logging, at a level low enough to keep the correlated lines — the shipped devnet config's `debug` does, the mainnet config's `warning` suppresses them). **Not `{job="xrpld"}`** — see the note under Test 3 Step 3.

If nothing appears, check the collector logs for auth/export errors:

```bash
docker compose -f docker/telemetry/docker-compose.yml \
    -f docker/telemetry/docker-compose.grafanacloud.yaml \
    logs otel-collector | grep -iE 'grafanacloud|401|403|export'
```

A `401`/`403` means the instance id or token is wrong; a connection error
means the endpoint URL is wrong or missing the `/otlp` path.

---

## Test 3: Log-Trace Correlation

xrpld injects `trace_id` and `span_id` into its log output when
a log line is emitted within an active OTel span. This test verifies the
end-to-end log-trace correlation pipeline.

### Step 1: Verify trace_id in log output

After running Test 1 or Test 2 (which generate RPC spans), check the
xrpld debug.log for trace context. A Test 1 run writes
`docker/telemetry/data/logs/xrpld-devnet/debug.log`; the mainnet config writes
`docker/telemetry/data/logs/mainnet/debug.log` instead.

```bash
grep 'trace_id=[a-f0-9]\{32\} span_id=[a-f0-9]\{16\}' \
    docker/telemetry/data/logs/xrpld-devnet/debug.log
```

Expected: log lines with `trace_id=<32hex> span_id=<16hex>` between the
severity code and the message. Example:

```
2024-Jan-15 10:30:45.123456789 UTC RPCHandler:DBG trace_id=abc123def456789012345678abcdef01 span_id=0123456789abcdef RPC call server_info completed in 0.000123seconds
```

That example is a Test 1 line. `xrpld-telemetry.cfg` logs at `debug`, so the
in-span RPC statement above appears. Test 2's nodes log at `info`, which
suppresses it — there, look for the `CNF Val` / `CNF buildLCL` lines from the
consensus accept span instead. Either carries trace context; only the message
differs.

Lines emitted outside of an active span (background tasks, startup) will
NOT have trace context — this is expected.

### Step 2: Cross-check trace_id in Tempo

Extract a `trace_id` from the log and verify it exists in Tempo:

```bash
TRACE_ID=$(grep -m1 -o 'trace_id=[a-f0-9]\{32\}' \
    docker/telemetry/data/logs/xrpld-devnet/debug.log | cut -d= -f2)
echo "Checking trace: $TRACE_ID"
curl -s "http://localhost:3200/api/traces/$TRACE_ID" | jq '.batches | length'
```

Expected result: `> 0` (the trace exists in Tempo).
Tempo returns the trace in OTLP shape, so the array is `batches`, not `data`,
and one trace can arrive as several batches.

### Step 3: Verify Loki log ingestion

The OTel Collector's file_log receiver tails xrpld's debug.log and
exports parsed entries to Loki. Verify Loki has received entries:

```bash
# Query Loki for any xrpld logs in the last 10 minutes
NOW_NS=$(($(date +%s) * 1000000000))
curl -sG "http://localhost:3100/loki/api/v1/query_range" \
    --data-urlencode 'query={service_name="xrpld"}' \
    --data-urlencode "start=$((NOW_NS - 600000000000))" \
    --data-urlencode "end=${NOW_NS}" \
    --data-urlencode 'limit=5' \
    --data-urlencode 'direction=backward' |
    jq '[.data.result[].values | length] | add // 0'
```

Expected: > 0 log lines.

Use `query_range`, not `query`. Loki rejects a bare log selector on the
instant `/query` endpoint with HTTP 400 and a `text/plain` body
("log queries are not supported as an instant query type"), so `jq` fails to
parse it and the step never prints a number — even when ingestion is working.
Only metric queries such as `sum(count_over_time(...))` are allowed there, so a
check that needs a count rather than the lines themselves can use the instant
endpoint. `query_range` timestamps are unix nanoseconds.
Counting `.data.result | length` would count streams, not log lines.

> **Use `service_name`, not `job`.** The local stack's `resource/logs` processor
> sets one key, `service.name=xrpld`, in `otel-collector-config.yaml`; its
> comment there explains that a custom `job` attribute is not promoted to a
> stream label and tells you to select on `service_name`. Only the Grafana Cloud
> variant also sets `job=xrpld`, in `otel-collector-config.grafanacloud.yaml`.
> Either way `{job="xrpld"}` does not work as a selector: on OTLP ingest Loki
> promotes only an allow-listed set of resource attributes to indexed stream
> labels. On the pinned `grafana/loki:3.7.6` that list is a fixed 18 keys,
> including `service.name` → `service_name`, `service.namespace`,
> `service.instance.id`, `deployment.environment` and `container.name`. `k8s.*`
> and `cloud.*` are enumerated key lists (ten and two entries), not wildcards.
> `job` is not on the list. This repo mounts no Loki config override — the `loki`
> service runs the image's built-in `/etc/loki/local-config.yaml`
> named in `docker-compose.yml` — so `job` lands in **structured metadata**, which
> cannot be a stream selector. `{job="xrpld"}` therefore returns **zero results
> with no error**, which reads exactly like "logs are not being ingested". If
> this query is empty, check `{service_name="xrpld"}` before debugging the
> pipeline. All 35 Loki queries in the shipped dashboards select on
> `service_name`; none uses `job`.

### Step 4: Verify Grafana Tempo-to-Loki correlation

1. Open Grafana at http://localhost:3000
2. Navigate to **Explore** -> select **Tempo** datasource
3. Search for a trace (e.g., operation `rpc.command.server_info`)
4. Expand a span and click **"Logs for this span"** in its **Links** row
5. Verify that Loki log lines appear, filtered by the trace's `trace_id`

### Step 5: Verify Grafana Loki-to-Tempo correlation

1. In Grafana **Explore**, select **Loki** datasource
2. Query: `{service_name="xrpld"} |= "trace_id="`
3. In the log results, click the **TraceID** derived field link
4. Verify it navigates to the full trace in Tempo

### Expected results

| Check                       | Expected                                 |
| --------------------------- | ---------------------------------------- |
| `trace_id=` in debug.log    | Present in log lines within active spans |
| `span_id=` in debug.log     | Present alongside trace_id               |
| Logs without active span    | No trace_id/span_id fields               |
| trace_id in Tempo           | Matches a valid trace                    |
| Loki log ingestion          | Logs visible via LogQL                   |
| Tempo -> Loki span log link | Shows correlated log lines               |
| Loki -> Tempo TraceID link  | Navigates to correct trace               |

---

## Troubleshooting

### No traces in Tempo

1. Check otel-collector logs:
   ```bash
   docker compose -f docker/telemetry/docker-compose.yml logs otel-collector
   ```
2. Verify xrpld telemetry config has `enabled=1` and correct endpoint
3. Check the collector is up — the readiness check in Test 1 Step 1. Probe
   `health_check` on 13133, not the OTLP/HTTP port 4318, which answers 404 to a
   `GET /`
4. Increase `batch_delay_ms` or decrease `batch_size` in xrpld config

### Nodes not reaching "proposing" state

1. Check that all peer ports (51235-51240) are not in use:
   ```bash
   for p in 51235 51236 51237 51238 51239 51240; do
       ss -tlnp | grep ":$p " && echo "port $p in use"
   done
   ```
2. Verify `[ips_fixed]` lists the 5 other peer ports, and not the node's own
3. Verify `validators.txt` has all 6 public keys
4. Check node debug logs: `tail -50 /tmp/xrpld-integration/Node-1/debug.log`
5. Ensure `[peer_private]` is set to `1`. In `src/libxrpl/peerfinder/Config.cpp`
   it sets both `autoConnect = !standalone && !peerPrivate` and
   `wantIncoming = (!config.peerPrivate) && (port != 0)`, so it stops the node
   reaching out to the public network **and** stops it accepting inbound peers.
   The nodes here find each other through `[ips_fixed]`, which is unaffected.

### Transaction not processing

1. Verify genesis account exists:
   ```bash
   curl -s http://localhost:5005 \
       -d '{"method":"account_info","params":[{"account":"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"}]}' |
       jq .result.account_data.Balance
   ```
2. Check submit response for error codes
3. In standalone mode, remember to call `ledger_accept` after submitting

### No trace_id in log output

1. Verify xrpld was built with `telemetry=ON` (`-Dtelemetry=ON` in CMake)
2. Verify `enabled=1` in the `[telemetry]` config section
3. Log lines only contain trace context when emitted inside an active span.
   Background logs (startup, periodic tasks outside spans) will not have
   `trace_id`/`span_id`.
4. Ensure the trace category is enabled (e.g., `trace_rpc=1` for RPC logs)

### No logs in Loki

1. Verify the log file mount in docker-compose.yml:
   ```yaml
   volumes:
     - ${XRPLD_LOG_DIR:-./data/logs}:/var/log/xrpld:ro
   ```
   The mount source defaults to the repo-relative `docker/telemetry/data/logs`
   (where the telemetry configs write). Override `XRPLD_LOG_DIR` to tail logs
   from another root.
2. Check OTel Collector logs for file_log receiver errors:
   ```bash
   docker compose -f docker/telemetry/docker-compose.yml logs otel-collector | grep -i "file_log\|loki\|error"
   ```
3. Verify Loki is running:
   ```bash
   curl -s http://localhost:3100/ready
   ```
4. Verify the file_log receiver glob pattern matches your log files:
   The default pattern is `/var/log/xrpld/*/debug.log`

### Grafana trace-log links not working

1. Verify `tracesToLogs` is configured in the Tempo datasource provisioning
   (`docker/telemetry/grafana/provisioning/datasources/tempo.yaml`)
2. Verify `derivedFields` is configured in the Loki datasource provisioning
   (`docker/telemetry/grafana/provisioning/datasources/loki.yaml`)
3. Restart Grafana after changing provisioning files:
   ```bash
   docker compose -f docker/telemetry/docker-compose.yml restart grafana
   ```

### Spanmetrics not appearing in Prometheus

1. Verify otel-collector config has `span_metrics` connector
2. Check that the metrics pipeline matches `otel-collector-config.yaml`
   verbatim:
   ```yaml
   service:
     pipelines:
       metrics:
         receivers: [otlp, span_metrics]
         processors: [resource/tier, resource/stripsdk, batch]
         exporters: [prometheus]
   ```
   Both receivers are required. `span_metrics` carries the span-derived
   `span_*` series; `otlp` carries the node's native `beast::insight` /
   MetricsRegistry metrics, which arrive on the same OTLP port. Dropping
   `otlp` silently removes every native metric while the `span_*` ones keep
   working — so the dashboards only half-break. (The cloud config,
   `otel-collector-config.grafanacloud.yaml`, spells the same connector
   `spanmetrics`; both are valid ids for it.)
3. Verify Prometheus can reach collector:
   ```bash
   curl -s http://localhost:9090/api/v1/targets | jq '.data.activeTargets'
   ```
