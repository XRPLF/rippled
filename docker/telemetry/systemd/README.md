# Running two xrpld instances on one host

Templates and an installer for running **two nodes side by side**, one per
storage backend, so NuDB and RocksDB can be compared with everything else equal.

These are committed deliberately. They previously existed only on the host and
were lost when that machine was rebuilt, which meant reconstructing the RocksDB
node's settings from notes.

## What differs between the two

Everything else is identical on purpose — any other divergence would confound the
backend comparison.

|                                   | instance 1                         | instance 2                          |
| --------------------------------- | ---------------------------------- | ----------------------------------- |
| Unit                              | `xrpld-mainnet`                    | `xrpld-mainnet2`                    |
| Tracked config                    | `xrpld-telemetry-mainnet.cfg`      | `xrpld-telemetry-mainnet2.cfg`      |
| Config the unit runs              | `xrpld-telemetry-mainnet.host.cfg` | `xrpld-telemetry-mainnet2.host.cfg` |
| `service_instance_id`             | `$NODE1_INSTANCE_ID`               | `$NODE2_INSTANCE_ID`                |
| Backend                           | NuDB                               | RocksDB                             |
| rpc / ws-admin / ws-public / peer | 5015 / 6016 / 6015 / 51245         | 5025 / 6026 / 6025 / 51255          |
| Data                              | `data/mainnet`                     | `data2/mainnet`                     |
| Logs                              | `data/logs/$NODE1_INSTANCE_ID/`    | `data/logs/$NODE2_INSTANCE_ID/`     |

Ports continue the offset-by-ten scheme already in use — devnet on 5005, Mainnet
on 5015 — so all three configs can bind on one host.

## No host-specific values in git

This repository is public, and an earlier commit already removed a personal home
directory from shipped config. The unit templates therefore carry placeholders,
and the real values live in an untracked `.env.devbox`:

```sh
cp docker/telemetry/.env.devbox.example docker/telemetry/.env.devbox
chmod 600 docker/telemetry/.env.devbox
$EDITOR docker/telemetry/.env.devbox
```

`.env.*` is gitignored, so the real file cannot be committed. The installer
refuses to run if the file is not mode `600`, and refuses to install a unit that
still contains an unsubstituted placeholder.

## Telemetry identity names the machine, so it is not committed

Each node reports a `service_instance_id` on every metric, span and log line.
It should name the **machine**, so a dashboard shows which box the data came
from — but a machine name is exactly what a public repository should not carry.

So the tracked configs keep a generic identity and name no host, and the
installer renders each one into a `.host.cfg` beside it with
`NODE1_INSTANCE_ID` / `NODE2_INSTANCE_ID` substituted in. The units run the
rendered copies; `*.host.cfg` is gitignored. The tracked configs are never
edited, so an update never conflicts and a rebuild loses nothing.

The identity is substituted in two places at once — the `service_instance_id`
setting and the log directory name — because they must agree, for the reason in
the next section. The installer counts the occurrences it expects to replace and
verifies the result, so a config reshuffle fails loudly instead of yielding a
copy that quietly kept the generic identity: on the dashboards that reads as the
node having disappeared, not as a failed substitution.

**Reuse the host's established names.** These values are the join key for
everything already stored. Renaming a node starts a fresh series and silently
breaks continuity with its own history — the old data is still there, under the
old name, and no dashboard will show both.

Re-run the installer after changing either the tracked config or `.env.devbox`;
nothing else regenerates the rendered copies.

## Install

```sh
sh docker/telemetry/systemd/install-units.sh
sudo systemctl start xrpld-mainnet          # wait for tracking/full
sudo systemctl start xrpld-mainnet2
```

Staggering the starts is a nicety rather than a requirement when the data
directories are on fast local storage, but two bootstrapping nodes still contend
for the job pool.

## Two things that are easy to get wrong

**The log directory basename must equal the `service_instance_id`, and sit in the
one log root.** The installer substitutes the id into both the setting and the
path from one value so they cannot drift, but the directory itself still has to
exist under that name. The collector's filelog receiver derives per-node identity
from the log path (`include_file_path` plus a regex on `/xrpld/<id>/debug.log`).
Name the directory anything else and that node's _logs_ lose their
`service_instance_id` label while its _metrics_ keep theirs — so the dashboards'
`$node` filter matches nothing for logs and reads as "no logs" rather than as a
misconfiguration.

The root is `data/logs`, which compose mounts into the collector as
`/var/log/xrpld`. That is a path inside the container, not on the host, so
nothing needs creating in the host's `/var/log`. **Both** nodes log under
`data/logs`, including the one whose nodestore is under `data2/`: a log directory
outside the mounted root is never read at all, and since both data directories
sit on the same disk, splitting the logs would buy no I/O separation to pay for
the lost collection.

**Put the data directories on fast local storage.** The configs use
repo-relative paths so they stay portable; point them at the fast disk with
symlinks:

```sh
mkdir -p "$DATA_MOUNT"/xrpld/data "$DATA_MOUNT"/xrpld/data2
ln -sfn "$DATA_MOUNT"/xrpld/data  docker/telemetry/data
ln -sfn "$DATA_MOUNT"/xrpld/data2 docker/telemetry/data2
```

Measured on an i4i instance: moving the nodestore off EBS onto the instance-store
NVMe took time-to-`full` from 2234 s to 681 s (3.3x), because NuDB's roughly
tenfold key-file write amplification saturated EBS while the local NVMe sat near
idle.

If that storage is an instance store, note it is **volatile** — contents survive
a reboot but are lost on a stop/start. Both units carry `RequiresMountsFor`, so a
missing mount fails the unit loudly instead of silently filling the root
filesystem.

## Telemetry

Both instances export OTLP to the collector on `localhost:4318`. Bring the
collector up with **both** compose files:

```sh
docker compose -f docker/telemetry/docker-compose.yml \
               -f docker/telemetry/docker-compose.grafanacloud.yaml up -d --force-recreate
```

The base file alone yields a collector with only local exporters, so telemetry
silently never leaves the host and the Cloud dashboards read empty with no error
anywhere. `--force-recreate` is needed after a config change, or Compose reuses
the running container with its old config.

Grafana Cloud credentials go in `.env.grafanacloud`, also gitignored, also mode
`600`.
