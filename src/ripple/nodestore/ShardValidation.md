# Downloaded Shard Validation

## Overview

In order to validate shards that have been downloaded from file servers (as opposed to shards acquired from peers), the application must confirm the validity of the downloaded shard's last ledger. The following sections describe this confirmation process in greater detail.

## Execution Concept

### Flag Ledger

Since the number of ledgers contained in each shard is always a multiple of 256, a shard's last ledger is always a flag ledger. Conveniently, the application provides a mechanism for retrieving the hash for a given flag ledger:

```C++
boost::optional<uint256>
hashOfSeq (ReadView const& ledger,
           LedgerIndex seq,
           beast::Journal journal)
```

When validating downloaded shards, we use this function to retrieve the hash of the shard's last ledger. If the function returns a hash that matches the hash stored in the shard, validation of the shard can proceed.

### Caveats

#### Later Ledger

The `getHashBySeq` function will provide the hash of a flag ledger only if the application has stored a later ledger. When validating a downloaded shard, if there is no later ledger stored, validation of the shard will be deferred until a later ledger has been stored.

We employ a simple heuristic for determining whether the application has stored a ledger later than the last ledger of the downloaded shard:

```C++
// We use the presence (or absense) of the validated
// ledger as a heuristic for determining whether or
// not we have stored a ledger that comes after the
// last ledger in this shard. A later ledger must
// be present in order to reliably retrieve the hash
// of the shard's last ledger.
if (app_.getLedgerMaster().getValidatedLedger())
{
    auto const hash = app_.getLedgerMaster().getHashBySeq(
        lastLedgerSeq(shardIndex));

    .
    .
    .
}
```

The `getHashBySeq` function will be invoked only when a call to `LedgerMaster::getValidatedLedger` returns a validated ledger, rather than a `nullptr`. Otherwise validation of the shard will be deferred.

### Retries

#### Retry Limit

If the server must defer shard validation, the software will initiate a timer that upon expiration, will re-attempt confirming the last ledger hash. We place an upper limit on the number of attempts the server makes to achieve this confirmation. When the maximum number of attempts has been reached, validation of the shard will fail, resulting in the removal of the shard. An attempt counts toward the limit only when we are able to get a validated ledger (implying a current view of the network), but are unable to retrieve the last ledger hash. Retries that occur because no validated ledger was available are not counted.

#### ShardInfo

The `DatabaseShardImp` class stores a container of `ShardInfo` structs, each of which contains information pertaining to a shard held by the server. These structs will be used during shard import to store the last ledger hash (when available) and to track the number of hash confirmation attempts that have been made.

```C++
struct ShardInfo
{
    .
    .
    .

    // Used to limit the number of times we attempt
    // to retrieve a shard's last ledger hash, when
    // the hash should have been found. See
    // scheduleFinalizeShard(). Once this limit has
    // been exceeded, the shard has failed and is
    // removed.
    bool
    attemptHashConfirmation()
    {
        if (lastLedgerHashAttempts + 1 <= maxLastLedgerHashAttempts)
        {
            ++lastLedgerHashAttempts;
            return true;
        }

        return false;
    }

    // This variable is used during the validation
    // of imported shards and must match the
    // imported shard's last ledger hash.
    uint256 lastLedgerHash;

    // The number of times we've attempted to
    // confirm this shard's last ledger hash.
    uint16_t lastLedgerHashAttempts;

    // The upper limit on attempts to confirm
    // the shard's last ledger hash.
    static const uint8_t maxLastLedgerHashAttempts = 5;
};
```

### Shard Import

Once a shard has been successfully downloaded by the `ShardArchiveHandler`, this class invokes the `importShard` method on the shard database:

```C++
bool
DatabaseShardImp::importShard(
    std::uint32_t shardIndex,
    boost::filesystem::path const& srcDir)
```

At the end of this method, `DatabaseShardImp::finalizeShard` is invoked which begins validation of the downloaded shard. This will be changed so that instead, the software first creates a task to confirm the last ledger hash. Upon the successful completion of this task, shard validation will begin.

```C++
bool
DatabaseShardImp::importShard(
    std::uint32_t shardIndex,
    boost::filesystem::path const& srcDir)
{
    .
    .
    .

    taskQueue_->addTask([this]()
    {
        // Verify hash.
        // Invoke DatabaseShardImp::finalizeShard on success.
        // Defer task if necessary.
    });
}
```
