# Open Shard Management

## Overview

Shard NuDB and SQLite databases consume server resources. This can be unnecessarily taxing on servers with many shards. The open shard management feature aims to improve the situation by managing a limited number of open shard database connections. The feature, which is integrated into the existing DatabaseShardImp and Shard classes, maintains a limited pool of open databases prioritized by their last use time stamp. The following sections describe the feature in greater detail.

### Open Shard Management

The open shard management feature is integrated into the DatabaseShardImp and Shard classes. As the DatabaseShardImp sweep function is periodically called, the number of finalized open shards, which constitutes the open pool, are examined. Upon the pool exceeding a pool limit, an attempt is made to close enough open shards to remain within the limit. Shards to be closed are selected based on their last use time stamp, which is automatically updated on database access. If necessary, shards will automatically open their databases when accessed.

```C++
    if (openFinals.size() > openFinalLimit_)
    {
        // Try to close enough shards to be within the limit.
        // Sort on largest elapsed time since last use.
        std::sort(
            openFinals.begin(),
            openFinals.end(),
            [&](std::shared_ptr<Shard> const& lhsShard,
                std::shared_ptr<Shard> const& rhsShard) {
                return lhsShard->getLastUse() > rhsShard->getLastUse();
            });

        for (auto it{openFinals.cbegin()};
             it != openFinals.cend() && openFinals.size() > openFinalLimit_;)
        {
            if ((*it)->tryClose())
                it = openFinals.erase(it);
            else
                ++it;
        }
    }
```

### Shard

When closing an open shard, DatabaseShardImp will call the Shard 'tryClose' function. This function will only close the shard databases if there are no outstanding references.

DatabaseShardImp will use the Shard 'isOpen' function to determine the state of a shard's database.

### Caveats

The Shard class must check the state of its databases before use. Prior use assumed databases were always open, that is no longer the case with the open shard management feature.
