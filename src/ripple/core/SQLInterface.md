# SQL Interface

The following changes were made to the rippled code:

1) all SQL hard code (except of sqlite-specific tests) moved to the single place;
2) provided pure interface class which can have several implementations for different SQL database types;
3) done substitution of databases for the future use: if the node database is absent then shard databases used instead.

## Configuration

The following changes to rippled configuration file is proposed. New parameter `sql_backend` will be added to  `[node_db]` and  `[shard_db]` sections. The value of this parameter is the name of SQL implementation used for node or shard databases. At the present, the only valid value of this parameter is `sqlite`. 

## Files

These source files are new or moved from other location:

- `ripple/core/SQLInterface.h` - new file, contains definition of main pure classes of the interface: `SQLInterface` and `SQLDatabase`;
- `ripple/core/impl/SQLInterface.cpp` - new file, implementation of static methods of the class `SQLInterface`;
- `ripple/core/sql_backend/SQLInterface_sqlite.cpp` - new file, implementation of `SQLInterface` and `SQLDatabase` classes for the case of sqlite databases;
- `ripple/core/sql_backend/DatabaseCon.h` - moved from `ripple/core/DatabaseCon.h` because this file is specific to sqlite implementation;
- `ripple/core/sql_backend/DatabaseCon.cpp` - moved from `ripple/core/impl/DatabaseCon.h` because this file is specific to sqlite implementation;
- `ripple/core/sql_backend/SociDB.h` - moved from `ripple/core/SociDB.h` because this file is specific to sqlite implementation;
- `ripple/core/sql_backend/SociDB.cpp`- moved from `ripple/core/impl/SociDB.cpp` because this file is specific to sqlite implementation.

## Classes

The main class of the interface is `class SQLInterface` defined in the file `ripple/core/SQLInterface.h`. This class has both static methods aplied to all SQL implementations and non-static methods which should be provided by each implementation. Static methods are implemented in the file `ripple/core/impl/SQLInterface.cpp`. At present there is one implementation of the interface - `class SQLInterface_sqlite` for sqlite database. It is located in the file `ripple/core/sql_backend/SQLInterface_sqlite.cpp`.

Methods of the SQL interface deal with SQL databases by using the class `class SQLDatabase`. This class represents pointer to single SQL database of unspecified internal format. Class `SQLDatabase` is implemented as `std::unique_ptr` to abstract class  `SQLDatabase_`. The class `SQLDatabase_` represents single SQL database of unspecified internal format. There is one implementation of class `SQLDatabase_` - class `SQLDatabase_sqlite`  located in the file `ripple/core/sql_backend/SQLInterface_sqlite.cpp`. 

Class `SQLDatabase_sqlite` is designed as `std::variant` with alternative types `DatabaseCon`,  `soci::session` and `bool`. `DatabaseCon` is used in most cases as basic implementation of sqlite database. In rare cases `soci::session` is used directly instead of `DatabaseCon`. For the future implementation of absent node database the class `SQLDatabase_sqlite` for such an absent database should still be created using the `bool` alternative type.

## Methods 

There are 3 types of non-static methods os SQLInterface:

1) Methods for creation/initialization of the database. Methods for the creation of database create one or several `SQLDatabase` objects and return it (note that  `SQLDatabase` is `unique_ptr` to the actual `SQLDatabase_` class). Methods for database initialization obtain link to `SQLDatabase` as  parameter, create `SQLDatabase_` object and change the pointer in given `SQLDatabase` to created object.

2) Methods for usage of the database. Generally such a method obtains one or several existing databases as parameters. Specifically parameters are of the type `SQLDatabase &`.

3) Internal methods for the case of absent node database. Generally, such a method is similar to the corresponding method of type 2). It has the same name as the corresponding method and almost the same list of parameters. The exception is that the method of type 3) has  `SQLDatabase_ *` link type to the database instead of `SQLDatabase &`. 

These internal methods of type 3) are used as follows. If the external method of type 2) is called for node database and this database is present, then the corresponding method of class 3) is called for the same database to complete the operation. If the node database is absent, then the static internal maps of the class `SQLInterface` are used to determine which shard databases corresponds to this node database. 

Finally, the corresponding internal methods of type 3) are called for found shard databases to complete the operation. Note that shard databases may have another SQL implementation in comparison with node database. That's why methods of the type 3) are called for shard databases using the pointer to appropriate `SQLInterface` implementation, the pointer is stored in each  `SQLDatabase`

## Methods lists

Here is the list of methods of type 1. (for puprose of demonstration, not as a documentation):

```
makeLedgerDBs
makeAcquireDB
makeWalletDB
makeArchiveDB
initStateDB
openDatabaseBodyDb
makeVacuumDB
initPeerFinderDB
updatePeerFinderDB
```

Here is the list of the methods which have both type-2) and type-3) implementations:

```
getMinLedgerSeq
getMaxLedgerSeq
deleteByLedgerSeq
deleteBeforeLedgerSeq
getRows
getRowsMinMax
saveValidatedLedger
loadLedgerInfoByIndex
loadLedgerInfoByIndexSorted
loadLedgerInfoByIndexLimitedSorted
loadLedgerInfoByHash
getHashByIndex
getHashesByIndex
loadTxHistory
getAccountTxs
getAccountTxsB
accountTxPage
loadTransaction
```

Here is the list of the methods which have only type-2) implementation. The reason that this functions have no need in type-3) implementation is as follows: these functions are not called for node databases.

```
insertAcquireDBIndex
selectAcquireDBLedgerSeqs
selectAcquireDBLedgerSeqsHash
updateLedgerDBs
updateAcquireDB
loadManifest
saveManifest
checkDBSpace
loadNodeIdentity
databaseBodyDoPut
databaseBodyFinish
addValidatorManifest
loadPeerReservationTable
insertPeerReservation
deletePeerReservation
readArchiveDB
insertArchiveDB
deleteFromArchiveDB
getCanDelete
setCanDelete
getSavedState
setSavedState
setLastRotated
dropArchiveDB
getKBUsedAll
getKBUsedDB
readPeerFinderD
savePeerFinderDB
```

 