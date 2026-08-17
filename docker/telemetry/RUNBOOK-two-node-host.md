# Runbook: two-node xrpld host with Grafana Cloud telemetry

Operating a host that runs **two xrpld instances side by side** — one per storage
backend — exporting metrics, traces and logs to Grafana Cloud.

Instance layout, ports and the reasoning behind them are in
[systemd/README.md](systemd/README.md). This document is the operational
sequence: update, rebuild, run, verify.

## Guiding rule: never edit tracked files on the host

Everything host-specific belongs in the host-local settings the unit installer
reads, or in symlinks — never in a tracked config. A host that edits tracked
configs in place will hit merge conflicts on every update, and those edits are
lost the moment the machine is rebuilt. That has already happened once and cost a
full reconstruction of the second instance.

If a host genuinely needs a tracked file to differ, add a systemd drop-in or a new
host-local setting rather than editing the file.

---

## 1. One-time host setup

Run from the repository root once per machine.

**Toolchain and container runtime.** A C++ toolchain (`cmake`, `g++`, `make`,
`gdb`), `ccache`, Conan, and Docker with the Compose plugin. Install Conan so it
resolves on the **non-interactive** PATH — a user-local install works when you
type it but fails under remote automation with `conan: command not found`, long
after the host looks healthy.

Point Conan's cache at the fast local disk, not the root filesystem: it runs to
tens of GB of small-file I/O.

**Data directories on fast local storage.** Create them on the fast mount and
symlink them into the tree, so the configs stay portable:

```sh
mkdir -p "$FAST_MOUNT"/xrpld/data "$FAST_MOUNT"/xrpld/data2
ln -sfn "$FAST_MOUNT"/xrpld/data  docker/telemetry/data
ln -sfn "$FAST_MOUNT"/xrpld/data2 docker/telemetry/data2
```

**Log directories where the collector looks.** The collector reads
`/var/log/xrpld/<instance-id>/debug.log` and derives each node's identity from
that directory name, so the basename must match the instance id exactly:

```sh
mkdir -p "$FAST_MOUNT"/xrpld/data/logs/xrpld-mainnet \
         "$FAST_MOUNT"/xrpld/data2/logs/xrpld-mainnet2
sudo mkdir -p /var/log/xrpld
sudo ln -sfn "$FAST_MOUNT"/xrpld/data/logs/xrpld-mainnet   /var/log/xrpld/xrpld-mainnet
sudo ln -sfn "$FAST_MOUNT"/xrpld/data2/logs/xrpld-mainnet2 /var/log/xrpld/xrpld-mainnet2
```

**Host-local settings and cloud credentials.** Both are provided out of band and
are never committed; the ignore rules already exclude them. Copy each tracked
example in this directory to its working name, fill it in, and set mode `600`.
The unit installer refuses to run against a world-readable settings file.

**Install the units:**

```sh
sh docker/telemetry/systemd/install-units.sh
```

---

## 2. Update to the latest code on the branch in use

The working tree should contain **no tracked modifications** — only ignored files
and symlinks. Check first:

```sh
git status --porcelain
```

Expect empty output. Anything listed is a host edit that should have been a
host-local setting; resolve that before updating, or the next step will conflict.

```sh
BRANCH=$(git rev-parse --abbrev-ref HEAD)
git fetch origin "$BRANCH"
git merge --ff-only "origin/$BRANCH"
```

`--ff-only` is deliberate: a host should only ever move forward to what was
pushed. If it refuses, the host has diverged and that needs investigating rather
than forcing.

To move the host onto a **different** branch:

```sh
git fetch origin 'refs/heads/<namespace>/*:refs/remotes/origin/<namespace>/*'
git checkout -B <branch> "origin/<branch>"
```

The wide refspec is needed because a single-branch clone only tracks the branch it
was cloned with, and `git fetch origin <other>` populates `FETCH_HEAD` without
creating a remote-tracking ref.

---

## 3. Re-apply host overlays after an update

A checkout can replace a symlinked directory with a real one, and unit templates
may have changed. After every update:

```sh
# Confirm the data directories are still symlinks to the fast mount
ls -ld docker/telemetry/data docker/telemetry/data2

# Reinstall units in case the templates changed
sh docker/telemetry/systemd/install-units.sh
```

Verify the ignored host files survived — they should, since a checkout does not
touch ignored paths, but confirm their mode is still `600` before relying on
them.

---

## 4. Build

Stop the nodes first if the binary is in use.

```sh
sudo systemctl stop xrpld-mainnet xrpld-mainnet2

conan install . --output-folder .build --build missing --settings build_type=Release

cd .build
cmake .. -DCMAKE_TOOLCHAIN_FILE=build/Release/generators/conan_toolchain.cmake \
         -DCMAKE_BUILD_TYPE=Release
cmake --build . --target xrpld --parallel "$(nproc)"
cd ..
```

Notes from experience:

- The first `conan install` on a fresh host compiles dependencies from source and
  takes far longer than later runs. Subsequent builds reuse the cache.
- A full link peaks around 30 GB of RSS. On a memory-constrained host reduce
  `--parallel` rather than letting the OOM killer take the build — or worse, take
  a running node.
- If `conan install` reports a missing default profile, the cache directory was
  created by a different user than the one running Conan. Fix ownership, then
  `conan profile detect`.
- A dependency added upstream will fail `cmake` configure with a missing package
  before any compilation starts. Re-run `conan install` rather than assuming the
  build itself broke.

Confirm the binary is newer than the source you just pulled:

```sh
ls -l .build/xrpld
git log -1 --format='%h %s'
```

---

## 5. Run the collector

**Bring it up with both compose files.** The base file alone yields a collector
with only local exporters — telemetry silently never leaves the host, the Cloud
dashboards read empty, and nothing logs an error. This has caused lost
measurement runs more than once.

```sh
docker compose -f docker/telemetry/docker-compose.yml \
               -f docker/telemetry/docker-compose.grafanacloud.yaml \
               up -d --force-recreate
```

`--force-recreate` matters after any config change: without it Compose reuses the
running container with its old configuration, so the change appears applied but
is not.

Check it is up and quiet:

```sh
docker ps --format '{{.Names}}\t{{.Status}}'
docker logs --since 5m <collector-container> 2>&1 | grep -iE 'error|refused' | head
```

To confirm the cloud overlay is actually in effect, check which config file is
bind-mounted into the container rather than reading the container's command line —
the overlay mounts its config over the same path the base file uses, so the
command line looks identical either way:

```sh
docker inspect <collector-container> \
  --format '{{range .Mounts}}{{.Source}} -> {{.Destination}}{{"\n"}}{{end}}' | grep -i config
```

---

## 6. Run the nodes

Start them one at a time, letting the first reach `tracking` or `full` before
starting the second:

```sh
sudo systemctl start xrpld-mainnet
# poll until server_state is tracking or full, then:
sudo systemctl start xrpld-mainnet2
```

Health check per instance, against its own admin RPC port (see
[systemd/README.md](systemd/README.md) for which port belongs to which):

```sh
curl -s --max-time 8 -H 'Content-Type: application/json' \
  -d '{"method":"server_info","params":[{}]}' http://127.0.0.1:<rpc_port>/ \
  | python3 -c 'import json,sys; i=json.load(sys.stdin)["result"]["info"]; \
print(i["server_state"], i["peers"], (i.get("validated_ledger") or {}).get("seq"))'
```

Both processes should be running from the freshly built binary — worth checking
explicitly after a rebuild, since a unit that failed to restart leaves the old
process in place:

```sh
for p in $(pgrep -f '[x]rpld --conf'); do readlink -f "/proc/$p/exe"; done
```

---

## 7. Verify telemetry end to end

Do not infer success from the exporter configuration — read the data back out of
Grafana Cloud. Check all three signals, and check that each carries the right
per-instance identity:

- **Metrics** — both instance ids should appear as label values, with fresh
  samples. A metric family present for one instance and absent for the other
  usually means one node has not reached the state that emits it, not that the
  exporter is broken.
- **Traces** — spans from both instances, with plausible durations.
- **Logs** — lines from both instances, and critically each carrying its
  instance-id label. If metrics have the label but logs do not, the log directory
  basename does not match the instance id.
- **Log-to-trace correlation** — log lines should carry trace and span ids.

---

## 8. When something looks wrong

| Symptom | First thing to check |
| --- | --- |
| Cloud dashboards empty, no errors anywhere | Collector started without the cloud overlay, or without `--force-recreate` after a config change |
| Metrics have instance labels, logs do not | Log directory basename does not equal the instance id |
| Unit refuses to start | The data mount is missing; the units require it deliberately, so a node cannot silently fill the root filesystem |
| `conan: command not found` under automation | Conan installed user-locally instead of on the system PATH |
| Node runs an old binary after rebuild | The unit did not restart; stop both, rebuild, start again |
| Update refuses to fast-forward | The host has local commits or edits to tracked files — see the guiding rule at the top |

Counter-intuitive signals worth knowing before drawing conclusions:

- A **higher** count of acquisition timeouts or sweep evictions does not by itself
  indicate a stalled node; on a faster host they can rise because the node retries
  its way forward more cheaply. Judge sync health by tree-fetch completion rate
  instead.
- Outbound byte counters are recorded when the node decides to send, not when the
  bytes reach the wire, so under a flood they can overstate egress by orders of
  magnitude. Compare against the interface counters before believing them.
