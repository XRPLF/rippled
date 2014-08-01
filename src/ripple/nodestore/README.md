# NodeStore

## Introduction

A `NodeObject` is a simple object that the Ledger uses to store entries. It is 
comprised of a type, a hash, a ledger index and a blob. It can be uniquely 
identified by the hash, which is a 256 bit hash of the blob. The blob is a 
variable length block of serialized data. The type identifies what the blob 
contains. The fields are as follows: 
    
* `mType`

 An enumeration that determines what the blob holds. There are four 
 different types of objects stored. 

 * **ledger**
    
   A ledger header.

 * **transaction**
      
   A signed transaction.
      
 * **account node**
        
   A node in a ledger's account state tree.
        
 * **transaction node**
        
   A node in a ledger's transaction tree.
    
* `mHash`

 A 256-bit hash of the blob.

* `mLedgerIndex`
      
 An unsigned integer that uniquely identifies the ledger in which this 
   NodeObject appears.

* `mData`
      
 A blob containing the payload. Stored in the following format.
 
|Byte   |                     |                          |
|:------|:--------------------|:-------------------------|
|0...3  |ledger index         |32-bit big endian integer |
|4...7  |reserved ledger index|32-bit big endian integer |
|8      |type                 |NodeObjectType enumeration|
|9...end|data                 |body of the object data   |
---    
The `NodeStore` provides an interface that stores, in a persistent database, a 
collection of NodeObjects that rippled uses as its primary representation of 
ledger entries. All ledger entries are stored as NodeObjects and as such, need 
to be persisted between launches. If a NodeObject is accessed and is not in 
memory, it will be retrieved from the database.

## Backend

The `NodeStore` implementation provides the `Backend` abstract interface, 
which lets different key/value databases to be chosen at run-time. This allows 
experimentation with different engines. Improvements in the performance of the 
NodeStore are a constant area of research. The database can be specified in 
the configuration file [node_db] section as follows.

One or more lines of key / value pairs

Example:
```
type=RocksDB
path=rocksdb
compression=1
```
Choices for 'type' (not case-sensitive)
   
* **HyperLevelDB**
  
 An improved version of LevelDB (preferred).

* **LevelDB**

 Google's LevelDB database (deprecated).

* **none**

 Use no backend.

* **RocksDB**

 Facebook's RocksDB database, builds on LevelDB. 

* **SQLite**

 Use SQLite.

'path' speficies where the backend will store its data files.

Choices for 'compression'

* **0** off

* **1** on (default)