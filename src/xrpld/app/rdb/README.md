# Relational Database Interface

The guiding principles of the Relational Database Interface are summarized below:

* All hard-coded SQL statements should be stored in the [files](#source-files) under the `ripple/app/rdb` directory. With the exception of test modules, no hard-coded SQL should be added to any other file in rippled.
* The base class `RelationalDatabase` is inherited by derived classes that each provide an interface for operating on distinct relational database systems.
* For future use, the shard store will be used if the node store is absent.

## Overview

Firstly, the interface `RelationalDatabase` is inherited by the classes `SQLiteDatabase` and `PostgresDatabase` which are used to operate the software's main data store (for storing transactions, accounts, ledgers, etc.). Secondly, the files under the `detail` directory provide supplementary functions that are used by these derived classes to access the underlying databases. Lastly, the remaining files in the interface (located at the top level of the module) are used by varied parts of the software to access any secondary relational databases.

## Configuration

The config section `[relational_db]` has a property named `backend` whose value designates which database implementation will be used for node or shard databases. Presently the only valid value for this property is `sqlite`:

```
[relational_db]
backend=sqlite
```

## Source Files

The Relational Database Interface consists of the following directory structure (as of November 2021):

```
src/ripple/app/rdb/
├── backend
│   ├── detail
│   │   ├── impl
│   │   │   ├── Node.cpp
│   │   │   └── Shard.cpp
│   │   ├── Node.h
│   │   └── Shard.h
│   ├── impl
│   │   ├── PostgresDatabase.cpp
│   │   └── SQLiteDatabase.cpp
│   ├── PostgresDatabase.h
│   └── SQLiteDatabase.h
├── impl
│   ├── Download.cpp
│   ├── PeerFinder.cpp
│   ├── RelationalDatabase.cpp
│   ├── ShardArchive.cpp
│   ├── State.cpp
│   ├── UnitaryShard.cpp
│   ├── Vacuum.cpp
│   └── Wallet.cpp
├── Download.h
├── PeerFinder.h
├── RelationalDatabase.h
├── README.md
├── ShardArchive.h
├── State.h
├── UnitaryShard.h
├── Vacuum.h
└── Wallet.h
```

### File Contents
| File        | Contents    |
| ----------- | ----------- |
| `Node.[h\|cpp]` | Defines/Implements methods used by `SQLiteDatabase` for interacting with SQLite node databases|
| `Shard.[h\|cpp]` | Defines/Implements methods used by `SQLiteDatabase` for interacting with SQLite shard databases |
| <nobr>`PostgresDatabase.[h\|cpp]`</nobr> | Defines/Implements the class `PostgresDatabase`/`PostgresDatabaseImp` which inherits from `RelationalDatabase` and is used to operate on the main stores |
|`SQLiteDatabase.[h\|cpp]`| Defines/Implements the class `SQLiteDatabase`/`SQLiteDatabaseImp` which inherits from `RelationalDatabase` and is used to operate on the main stores |
| `Download.[h\|cpp]` | Defines/Implements methods for persisting file downloads to a SQLite database |
| `PeerFinder.[h\|cpp]` | Defines/Implements methods for interacting with the PeerFinder SQLite database |
|`RelationalDatabase.cpp`| Implements the static method `RelationalDatabase::init` which is used to initialize an instance of `RelationalDatabase` |
| `RelationalDatabase.h` | Defines the abstract class `RelationalDatabase`, the primary class of the Relational Database Interface |
| `ShardArchive.[h\|cpp]` | Defines/Implements methods used by `ShardArchiveHandler` for interacting with SQLite databases containing metadata regarding shard downloads |
| `State.[h\|cpp]` | Defines/Implements methods for interacting with the State SQLite database which concerns ledger deletion and database rotation |
| `UnitaryShard.[h\|cpp]` | Defines/Implements methods used by a unitary instance of `Shard` for interacting with the various SQLite databases thereof. These files are distinct from `Shard.[h\|cpp]` which contain methods used by `SQLiteDatabaseImp` |
| `Vacuum.[h\|cpp]` | Defines/Implements a method for performing the `VACUUM` operation on SQLite databases |
| `Wallet.[h\|cpp]` | Defines/Implements methods for interacting with Wallet SQLite databases |

## Classes

The abstract class `RelationalDatabase` is the primary class of the Relational Database Interface and is defined in the eponymous header file. This class  provides a static method `init()` which, when invoked, creates a concrete instance of a derived class whose type is specified by the system configuration. All other methods in the class are virtual. Presently there exist two classes that derive from `RelationalDatabase`, namely `SQLiteDatabase` and `PostgresDatabase`.

## Database Methods

The Relational Database Interface provides three categories of methods for interacting with databases:

* Free functions for interacting with SQLite databases used by various components of the software. These methods feature a `soci::session` parameter which facilitates connecting to SQLite databases, and are defined and implemented in the following files:

 * `Download.[h\|cpp]`
 * `PeerFinder.[h\|cpp]`
 * `ShardArchive.[h\|cpp]`
 * `State.[h\|cpp]`
 * `UnitaryShard.[h\|cpp]`
 * `Vacuum.[h\|cpp]`
 * `Wallet.[h\|cpp]`


* Free functions used exclusively by `SQLiteDatabaseImp` for interacting with  SQLite databases owned by the node store or shard store. Unlike the free functions in the files listed above, these are not intended to be invoked directly by clients. Rather, these methods are invoked by derived instances of `RelationalDatabase`. These methods are defined in the following files:

  * `Node.[h|cpp]`
  * `Shard.[h|cpp]`


* Member functions of `RelationalDatabase`, `SQLiteDatabase`, and `PostgresDatabase` which are used to access the main stores (node store, shard store). The `SQLiteDatabase` class will access the node store by default, but will use shard databases if the node store is not present and the shard store is available. The class `PostgresDatabase` uses only the node store.
