# Shard Storage Paths

## Overview

The shard database stores validated ledgers in logical groups called shards. As
of June 2020, a shard stores 16384 ledgers by default. In order to allow users
to store shards on multiple devices, the shard database can be configured with
several file system paths. Each path provided should refer to a directory on a
distinct filesystem, and no two paths should ever correspond to the same
filesystem. Violating this restriction will cause the server to inaccurately
estimate the amount of space available for storing shards. In the absence of a
suitable platform agnostic solution, this requirement is enforced only on
Linux. However, on other platforms we employ a heuristic that issues a warning
if we suspect that this restriction is violated.

## Configuration

The `shard_db` and `historical_shard_paths` sections of the server's
configuration file will be used to determine where the server stores shards.
Minimally, the `shard_db` section must contain a single `path` key.
If this is the only storage path provided, all shards will be stored at this
location. If the configuration also lists one or more lines in the
`historical_shard_paths` section, all older shards will be stored at these
locations, and the `path` will be used only to store the current
and previous shards. The goal is to allow users to provide an efficient SSD for
storing recent shards, as these will be accessed more frequently, while using
large mechanical drives for storing older shards that will be accessed less
frequently.

Below is a sample configuration snippet that provides a path for main storage
and several paths for historical storage:

```dosini
# This is the persistent datastore for shards. It is important for the health
# of the ripple network that rippled operators shard as much as practical.
# NuDB requires SSD storage. Helpful information can be found here
# https://ripple.com/build/history-sharding
[shard_db]
type=NuDB

# A single path for storing
# the current and previous
# shards:
# -------------------------
path=/var/lib/rippled/db/shards/nudb

# Path where shards are stored
# while being downloaded:
# ----------------------------
download_path=/var/lib/rippled/db/shards/

# The number of historical shards to store.
# The default value is 0, which means that
# the server won't store any historical
# shards - only the current and previous
# shards will be stored.
# ------------------------------------
max_historical_shards=100

# List of paths for storing older shards.
[historical_shard_paths]
/mnt/disk1
/mnt/disk2
/mnt/disk3

```
## Shard Migration

When a new shard (*current shard*) is confirmed by the network, the recent
shards will shift. The *previous shard* will become a *historical shard*, the
*current shard* will become the *previous shard*, and the new shard will become
the *current shard*. These are just logical labels, and the shards themselves
don't change to reflect being current, previous, or historical. However, if the
server's configuration specifies one or more paths for historical storage,
during this shift the formerly *previous shard* will be migrated to one of the
historical paths. If multiple paths are provided, the server dynamically
chooses one with sufficient space for storing the shard.

**Note:** As of June 2020, the shard database does not store the partial shard
currently being built by live network transactions, but this is planned to
change. When this feature is implemented, the *current shard* will refer to this
partial shard, and the *previous shard* will refer to the most recently
validated shard.

### Selecting a Historical Storage Path

When storing historical shards, if multiple historical paths are provided, the
path to use for each shard will be selected in a random fashion. By using all
available storage devices, we create a uniform distribution of disk utilization
for disks of equivalent size, (provided that the disks are used only to store
shards). In theory, selecting devices in this manner will also increase our
chances for concurrent access to stored shards, however as of June 2020
concurrent shard access is not implemented. Lastly, a storage path is included
in the random distribution only if it has enough storage capacity to hold the
next shard.

## Shard Acquisition

When the server is acquiring shard history, these acquired shards will be stored
at a path designated for historical storage (`historical_storage_path`). If no
such path is provided, acquired shards will be stored at the
`path`.

## Storage capacity

### Filesystem Capacity

When the shard database updates its record of disk utilization, it trusts that
the provided historical paths refer to distinct devices, or at least distinct
filesystems. If this requirement is violated, the database will operate with an
inaccurate view of how many shards it can store. Violation of this requirement
won't necessarily impede database operations, but the database will fail to
identify scenarios wherein storing the maximum number of historical shards (as
per the 'historical_shard_count' parameter in the configuration file) would
exceed the amount of storage space available.

### Shard Migration

During a "recent shard shift", if the server has already reached the configured
limit of stored historical shards, instead of moving the formerly *previous
shard* to a historical drive (or keeping it at the 'path') the
shard will be dropped and removed from the filesystem.

### Shard Acquisition

Once the configured limit of stored historical shards has been reached, shard
acquisition halts, and no additional shards will be acquired.
