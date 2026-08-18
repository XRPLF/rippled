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

**Host-local settings and cloud credentials.** Both are provided out of band and
are never committed; the ignore rules already exclude them. Copy each tracked
example in this directory to its working name, fill it in, and set mode `600`.
The unit installer refuses to run against a world-readable settings file.

The settings include each node's telemetry identity. Name those after the
machine, and **reuse the names this host has used before** — they are the join
key for every metric, trace and log already stored, so a new name starts a fresh
series and no dashboard will show the old and new data together. See
[systemd/README.md](systemd/README.md) for why they are not committed.

**Log directories where the collector looks.** The collector reads
`/var/log/xrpld/<instance-id>/debug.log` and derives each node's identity from
that directory name, so the basename must match the instance id exactly. Using
the ids from the settings file keeps the two in step:

```sh
. docker/telemetry/.env.devbox   # NODE1_INSTANCE_ID, NODE2_INSTANCE_ID

mkdir -p "$FAST_MOUNT"/xrpld/data/logs/"$NODE1_INSTANCE_ID" \
         "$FAST_MOUNT"/xrpld/data2/logs/"$NODE2_INSTANCE_ID"
sudo mkdir -p /var/log/xrpld
sudo ln -sfn "$FAST_MOUNT"/xrpld/data/logs/"$NODE1_INSTANCE_ID"  /var/log/xrpld/"$NODE1_INSTANCE_ID"
sudo ln -sfn "$FAST_MOUNT"/xrpld/data2/logs/"$NODE2_INSTANCE_ID" /var/log/xrpld/"$NODE2_INSTANCE_ID"
```

**Rootless Docker.** If the container runtime is rootless, its systemd user
units need a session bus to install, and the variable is absent over a plain
non-interactive SSH connection — the install appears to run and leaves nothing
behind:

```sh
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus
dockerd-rootless-setuptool.sh install
```

`DOCKER_HOST` must then point at the rootless socket for every later `docker`
invocation, including non-interactive ones.

**Install the units:**

```sh
sh docker/telemetry/systemd/install-units.sh
```

This also renders the host-local configs the units run
(`xrpld-telemetry-mainnet{,2}.host.cfg`), so re-run it after changing either a
tracked config or the settings file. Nothing else regenerates them.

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

# Reinstall units and re-render the host-local configs
sh docker/telemetry/systemd/install-units.sh
```

Re-running the installer is not optional after an update. It is what carries any
change to a tracked config into the `.host.cfg` the unit actually runs; skip it
and the node keeps running the previous rendering, so a config change appears to
have been applied and has not been.

Verify the ignored host files survived — they should, since a checkout does not
touch ignored paths, but confirm their mode is still `600` before relying on
them.

---

## 4. Build

Stop the nodes first if the binary is in use.

```sh
sudo systemctl stop xrpld-mainnet xrpld-mainnet2
```

**The daemon is not built by default.** The Conan recipe defaults its `xrpld`
option to off, so it has to be requested explicitly. Omit it and everything
appears to succeed — Conan and CMake both report success — and then the build
fails with `No rule to make target 'xrpld'`, because the target was never
created.

```sh
conan install . --output-folder .build --build missing \
      --settings build_type=Release -o '&:xrpld=True'

cmake --preset conan-release

cmake --build .build/build/Release --target xrpld --parallel "$(nproc)"
```

Three details that are easy to get wrong, each of which fails in a way that
misdirects:

- **Configure through the preset, not a hand-written toolchain path.** Conan
  writes the preset and the toolchain, and the toolchain does not sit where the
  single- versus multi-config layouts would suggest. Guessing the path yields
  `Could not find toolchain file`, followed by `CMAKE_CXX_COMPILER not set`, which
  reads as a broken compiler rather than a wrong path.
- **The build directory is `.build/build/Release`, not `.build`.** Building the
  wrong directory reports `Generator: execution of make failed`, which reads as a
  toolchain problem.
- **The CMake cache is sticky.** Changing a Conan option and re-running
  `conan install` does not change an already-cached CMake variable, so the
  daemon target stays absent even though the option was accepted. Confirm the
  value, and clear the cache if it disagrees:

  ```sh
  grep -E '^(xrpld|tests|telemetry):' .build/build/Release/CMakeCache.txt
  # if xrpld is not True:
  rm -f .build/build/Release/CMakeCache.txt
  rm -rf .build/build/Release/CMakeFiles
  cmake --preset conan-release
  ```

Further notes from experience:

- The first `conan install` on a fresh host compiles dependencies from source and
  takes far longer than later runs. Subsequent builds reuse the cache.
- A full link peaks around 30 GB of RSS. On a memory-constrained host reduce
  `--parallel` rather than letting the OOM killer take the build — or worse, take
  a running node.
- If Conan reports a missing default profile, its cache directory was created by a
  different user than the one running it. Fix ownership, then `conan profile
detect`.
- A dependency added upstream fails `cmake` configure with a missing package
  before any compilation starts. Re-run `conan install` rather than assuming the
  build itself broke.

**Expose the binary where the units expect it.** The build leaves `xrpld` in the
preset's build directory, while the units run `.build/xrpld`. Link the two, or
every start fails with the unit reporting only a missing executable:

```sh
ln -sfn build/Release/xrpld .build/xrpld
```

The installer warns when that path is not executable, which is the cheapest
place to catch it — before the unit is ever started.

Confirm the binary is newer than the source you just pulled:

```sh
find .build -maxdepth 4 -name xrpld -type f -printf '%p %s bytes %TY-%Tm-%Td %TH:%TM\n'
git log -1 --format='%h %s'
```

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

| Symptom                                                                 | First thing to check                                                                                                                                                                                          |
| ----------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Cloud dashboards empty, no errors anywhere                              | Collector started without the cloud overlay, or without `--force-recreate` after a config change                                                                                                              |
| Collector exits at startup on an unresolved authenticator               | An overlay redeclared `service.extensions`; the collector merges configs by **replacing** lists, not appending, so the cloud auth extension was dropped. Nothing exports at all in this state, local included |
| A node's data appears under the generic instance id, or a brand-new one | The installer was not re-run, so the `.host.cfg` still carries the old identity. Check the rendered file, not the tracked one                                                                                 |
| A node's history seems to have stopped                                  | Its identity changed; the old data is intact under the old name. Query both names                                                                                                                             |
| Metrics have instance labels, logs do not                               | Log directory basename does not equal the instance id                                                                                                                                                         |
| Unit refuses to start                                                   | The data mount is missing; the units require it deliberately, so a node cannot silently fill the root filesystem                                                                                              |
| `conan: command not found` under automation                             | Conan installed user-locally instead of on the system PATH                                                                                                                                                    |
| Node runs an old binary after rebuild                                   | The unit did not restart; stop both, rebuild, start again                                                                                                                                                     |
| Update refuses to fast-forward                                          | The host has local commits or edits to tracked files — see the guiding rule at the top                                                                                                                        |

Counter-intuitive signals worth knowing before drawing conclusions:

- A **higher** count of acquisition timeouts or sweep evictions does not by itself
  indicate a stalled node; on a faster host they can rise because the node retries
  its way forward more cheaply. Judge sync health by tree-fetch completion rate
  instead.
- Outbound byte counters are recorded when the node decides to send, not when the
  bytes reach the wire, so under a flood they can overstate egress by orders of
  magnitude. Compare against the interface counters before believing them.
