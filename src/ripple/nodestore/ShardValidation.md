# Downloaded Shard Validation

## Overview

In order to validate shards that have been downloaded from file servers (as opposed to shards acquired from peers), the application must confirm the validity of the downloaded shard's last ledger. So before initiating the download, we first confirm that we are able to retrieve the shard's last ledger hash. The following sections describe this confirmation process in greater detail.

## Hash Verification

### Flag Ledger

Since the number of ledgers contained in each shard is always a multiple of 256, a shard's last ledger is always a flag ledger. Conveniently, the skip list stored within a ledger will provide us with a series of flag ledger hashes, enabling the software to corroborate a shard's last ledger hash. We access the skip list by calling `LedgerMaster::walkHashBySeq` and providing the sequence of a shard's last ledger:

```C++
boost::optional<uint256> expectedHash;
expectedHash =
    app_.getLedgerMaster().walkHashBySeq(lastLedgerSeq(shardIndex));
```

When a user requests a shard download, the `ShardArchiveHandler` will first use this function to retrieve the hash of the shard's last ledger. If the function returns a hash, downloading the shard can proceed. Once the download completes, the server can reliably retrieve this last ledger hash to complete validation of the shard.

### Caveats

#### Later Ledger

The `walkHashBySeq` function will provide the hash of a flag ledger only if the application has stored a later ledger. When verifying the last ledger hash of a pending shard download, if there is no later ledger stored, the download will be deferred until a later ledger has been stored.

We use the presence (or absence) of a validated ledger with a sequence number later than the sequence of the shard's last ledger as a heuristic for determining whether or not we should have the shard's last ledger hash. A later ledger must be present in order to reliably retrieve the hash of the shard's last ledger. The hash will only be retrieved when a later ledger is present. Otherwise verification of the shard will be deferred.

### Retries

#### Retry Limit

If the server must defer hash verification, the software will initiate a timer that upon expiration, will re-attempt verifying the last ledger hash. We place an upper limit on the number of attempts the server makes to achieve this verification. When the maximum number of attempts has been reached, the download request will fail, and the `ShardArchiveHandler` will proceed with any remaining downloads. An attempt counts toward the limit only when we are able to get a later validated ledger (implying a current view of the network), but are unable to retrieve the last ledger hash. Retries that occur because no validated ledger was available are not counted.
