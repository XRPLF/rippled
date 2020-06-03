# Relational Database Interface

Here are main principles of Relational DB interface:

1) All SQL hard code is in the files described below in Files section.
No hard-coded SQL should be added to any other file in rippled, except related
to tests for specific SQL implementations.
2) Pure interface class `RelationalDBInterface` can have several
implementations for different relational database types.
3) For future use, if the node database is absent, then shard databases will
be used.

## Configuration

Section `[relational_db]` of the configuration file contains parameter
`backend`. The value of this parameter is the name of relational database
implementation used for node or shard databases. At the present, the only valid
value of this parameter is `sqlite`.

## Files

The following source files are related to Relational DB interface:

- `ripple/app/rdb/RelationalDBInterface.h` - definition of main pure class of
the interface, `RelationalDBInterface`;
- `ripple/app/rdb/impl/RelationalDBInterface.cpp` - implementation of static
method `init()` of the class `RelationalDBInterface`;
- `ripple/app/rdb/backend/RelationalDBInterfaceSqlite.h` - definition of pure
class `RelationalDBInterfaceSqlite` derived from `RelationalDBInterface`;
this is base class for sqlite implementation of the interface;
- `ripple/app/rdb/backend/RelationalDBInterfaceSqlite.cpp` - implementation of
`RelationalDBInterfaceSqlite`-derived class for the case of sqlite databases;
- `ripple/app/rdb/backend/RelationalDBInterfacePostgres.h` - definition of pure
class `RelationalDBInterfacePostgres` derived from `RelationalDBInterface`;
this is base class for postgres implementation of the interface;
- `ripple/app/rdb/backend/RelationalDBInterfacePostgres.cpp` - implementation
of `RelationalDBInterfacePostgres`-derived class for the case of postgres
databases;
- `ripple/app/rdb/RelationalDBInterface_global.h` - definitions of global
methods for all sqlite databases except of node and shard;
- `ripple/app/rdb/impl/RelationalDBInterface_global.cpp` - implementations of
global methods for all sqlite databases except of node and shard;
- `ripple/app/rdb/RelationalDBInterface_nodes.h` - definitions of global
methods for sqlite node databases;
- `ripple/app/rdb/impl/RelationalDBInterface_nodes.cpp` - implementations of
global methods for sqlite node databases;
- `ripple/app/rdb/RelationalDBInterface_shards.h` - definitions of global
methods for sqlite shard databases;
- `ripple/app/rdb/impl/RelationalDBInterface_shards.cpp` - implementations of
global methods for sqlite shard databases;
- `ripple/app/rdb/RelationalDBInterface_postgres.h` - definitions of internal
methods for postgres databases;
- `ripple/app/rdb/impl/RelationalDBInterface_postgres.cpp` - implementations of
internal methods for postgres databases;

## Classes

The main class of the interface is `class RelationalDBInterface`. It is defined
in the file `RelationalDBInterface.h`. This class has static method `init()`
which allow to create proper `RelationalDBInterface`-derived class specified
in the config. All other methods are pure virtual. These methods do not use
database as a parameter. It assumed that implementation of class derived from
`RelationalDBInterface` holds all database pointers inside and uses appropriate
databases (nodes or shards) to get return values required by each method.

At the present, there are two implementations of the derived classes -
`class RelationalDBInterfaceSqlite` for sqlite database (it is located in the
file `RelationalDBInterfaceSqlite.cpp`) and
`class RelationalDBInterfacePostgres` for postgres database (it is located in
the file `RelationalDBInterfacePostgres.cpp`)

## Methods 

There are 3 types of methods for SQL interface:

1) Global methods for work with all databases except of node. In particular,
methods related to shards datavases only. These methods are sqlite-specific.
They use `soci::session` as database pointer parameter. Defined and
implemented in files `RelationalDBInterface_global.*` and
`RelationalDBInterface_shard.*`.

2) Global methods for work with node databases, and also with shard databases.
For sqlite case, these methods are internal for `RelationalDBInterfaceSqlite`
implementation of the class `RelationalDBInterface`. They use `soci::session`
as database pointer parameter. Defined and implemented in files
`RelationalDBInterface_nodes.*`. For postgres case, these methods are internal
for `RelationalDBInterfacePostgres` implementation of the class
`RelationalDBInterface`. They use `std::shared_ptr<PgPool>` as database pointer
parameter. Defined and implemented in files `RelationalDBInterface_postgres.*`.

3) Virtual methods of class `RelationalDBInterface` and also derived classes
`RelationalDBInterfaceSqlite` and `RelationalDBInterfacePostgres`.
Calling such a method resulted in calling corresponding method from
`RelationalDBInterface`-derived class. For sqlite case, such a method tries to
retrieve information from node database, and if this database not exists - then
from shard databases. For both node and shard databases, calls to global
methods of type 2) performed. For postgres case, such a method retrieves
information only from node database by calling a global method of type 2).

## Methods lists

### Type 1 methods

#### Files RelationalDBInterface_global.*

Wallet DB methods:
```
makeWalletDB
makeTestWalletDB
getManifests
saveManifests
addValidatorManifest
getNodeIdentity
getPeerReservationTable
insertPeerReservation
deletePeerReservation
createFeatureVotes
readAmendments
voteAmendment
```

State DB methods:
```
initStateDB
getCanDelete
setCanDelete
getSavedState
setSavedState
setLastRotated
```

DatabaseBody DB methods:
```
openDatabaseBodyDb
databaseBodyDoPut
databaseBodyFinish
```

Vacuum DB method:
```
doVacuumDB
```

PeerFinder DB methods:
```
initPeerFinderDB
updatePeerFinderDB
readPeerFinderDB
savePeerFinderDB
```

#### Files RelationalDBInterface_shards.*

Shards DB methods:
```
makeShardCompleteLedgerDBs
makeShardIncompleteLedgerDBs
updateLedgerDBs
```

Shard acquire DB methods:
```
makeAcquireDB
insertAcquireDBIndex
selectAcquireDBLedgerSeqs
selectAcquireDBLedgerSeqsHash
updateAcquireDB
```

Shard archive DB methods:
```
makeArchiveDB
readArchiveDB
insertArchiveDB
deleteFromArchiveDB
dropArchiveDB
```

### Type 2 methods

#### Files RelationalDBInterface_nodes.*

```
makeLedgerDBs
getMinLedgerSeq
getMaxLedgerSeq
deleteByLedgerSeq
deleteBeforeLedgerSeq
getRows
getRowsMinMax
saveValidatedLedger
getLedgerInfoByIndex
getOldestLedgerInfo
getNewestLedgerInfo
getLimitedOldestLedgerInfo
getLimitedNewestLedgerInfo
getLedgerInfoByHash
getHashByIndex
getHashesByIndex
getHashesByIndex
getTxHistory
getOldestAccountTxs
getNewestAccountTxs
getOldestAccountTxsB
getNewestAccountTxsB
oldestAccountTxPage
newestAccountTxPage
getTransaction
DbHasSpace
```

#### Files RelationalDBInterface_postgres.*

```
getMinLedgerSeq
getMaxLedgerSeq
getCompleteLedgers
getValidatedLedgerAge
getNewestLedgerInfo
getLedgerInfoByIndex
getLedgerInfoByHash
getHashByIndex
getHashesByIndex
getTxHashes
getAccountTx
locateTransaction
writeLedgerAndTransactions
getTxHistory
```

### Type 3 methods

#### Files RelationalDBInterface.*

```
init
getMinLedgerSeq
getMaxLedgerSeq
getLedgerInfoByIndex
getNewestLedgerInfo
getLedgerInfoByHash
getHashByIndex
getHashesByIndex
getTxHistory
ledgerDbHasSpace
transactionDbHasSpace
```

#### Files backend/RelationalDBInterfaceSqlite.*

```
getTransactionsMinLedgerSeq
getAccountTransactionsMinLedgerSeq
deleteTransactionByLedgerSeq
deleteBeforeLedgerSeq
deleteTransactionsBeforeLedgerSeq
deleteAccountTransactionsBeforeLedgerSeq
getTransactionCount
getAccountTransactionCount
getLedgerCountMinMax
saveValidatedLedger
getLimitedOldestLedgerInfo
getLimitedNewestLedgerInfo
getOldestAccountTxs
getNewestAccountTxs
getOldestAccountTxsB
getNewestAccountTxsB
oldestAccountTxPage
newestAccountTxPage
oldestAccountTxPageB
newestAccountTxPageB
getTransaction
getKBUsedAll
getKBUsedLedger
getKBUsedTransaction
```

#### Files backend/RelationalDBInterfacePostgres.*

```
sweep
getCompleteLedgers
getValidatedLedgerAge
writeLedgerAndTransactions
getTxHashes
getAccountTx
locateTransaction
```
