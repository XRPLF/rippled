# Running two xrpld instances on one host

Templates and an installer for running **two nodes side by side**, one per
storage backend, so NuDB and RocksDB can be compared with everything else equal.

These are committed deliberately. They previously existed only on the host and
were lost when that machine was rebuilt, which meant reconstructing the RocksDB
node's settings from notes.

## What differs between the two

Everything else is identical on purpose — any other divergence would confound the
backend comparison.

| | instance 1 | instance 2 |
| --- | --- | --- |
| Unit | `xrpld-mainnet` | `xrpld-mainnet2` |
| Config | `xrpld-telemetry-mainnet.cfg` | `xrpld-telemetry-mainnet2.cfg` |
| `service_instance_id` | `xrpld-mainnet` | `xrpld-mainnet2` |
| Backend | NuDB | RocksDB |
| rpc / ws-admin / ws-public / peer | 5015 / 6016 / 6015 / 51245 | 5025 / 6026 / 6025 / 51255 |
| Data | `data/mainnet` | `data2/mainnet` |
| Logs | `data/logs/xrpld-mainnet/` | `data2/logs/xrpld-mainnet2/` |

Ports continue the offset-by-ten scheme already in use — devnet on 5005, Mainnet
on 5015 — so all three configs can bind on one host.

## No host-specific values in git

This repository is public, and an earlier commit already removed a personal home
directory from shipped config. The unit templates therefore carry placeholders,
and the real values live in an untracked `.env.devbox`:

```sh
cp docker/telemetry/.env.devbox.example docker/telemetry/.env.devbox
chmod 600 docker/telemetry/.env.devbox
$EDITOR docker/telemetry/.env.devbox      # RUN_USER, REPO_DIR, DATA_MOUNT
```

`.env.*` is gitignored, so the real file cannot be committed. The installer
refuses to run if the file is not mode `600`, and refuses to install a unit that
still contains an unsubstituted placeholder.

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

**The log directory basename must equal the `service_instance_id`.** The
collector's filelog receiver derives per-node identity from the log path
(`include_file_path` plus a regex on `/xrpld/<id>/debug.log`). Name the directory
anything else and that node's *logs* lose their `service_instance_id` label while
its *metrics* keep theirs — so the dashboards' `$node` filter matches nothing for
logs and reads as "no logs" rather than as a misconfiguration. The collector
expects the logs under `/var/log/xrpld/<id>/`, so symlink or bind-mount each
node's log directory there.

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
