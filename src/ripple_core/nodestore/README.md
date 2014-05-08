
# NodeStore

## Introduction

A NodeObject is a simple object that the Ledger uses to store entries. They 
are comprised of a type, a hash, a ledger index and a blob. 
    
    NodeObjectType m_type
      An enumeration with one of the following values: 
        hotUNKNOWN 
        hotLEDGER 
        hotTRANSACTION 
        hotACCOUNT_NODE
        hotTRANSACTION_NODE
    
    uint256 mHash
      A 256-bit hash of the blob

    LedgerIndex mLedgerIndex
      An unsigned integer that uniquely identifies the ledger this NodeObject 
      appears.

    Blob mData
      A buffer containing the payload.

A NodeStore can be uniquely identified by the hash, which is a SHA 256 of the 
blob. The blob is a variable length block of serialized data. The type 
identifies what the blob contains. 

The NodeStore provides an interface that stores, in a persistent database, a 
collection of NodeObjects that rippled uses as its primary representation of 
ledger entries. All ledger entries are stored as NodeObjects and as such, need 
to be persisted between launches. If a NodeObject is accessed and is not in 
memory, it will be retrieved from the database.

The persistent database used by the NodeStore can be specified in the 
configuration file node_db section. Details on configuring the database can be 
found in the rippled-exmaple.cfg file.

NodeStore objects are created using a factory pattern. To use one, create 
an instance of Manager and add at least one database type. Then call 
make_Database. You can then save or load NodeObjects by calling NodeStore.store 
or NodeStore.fetch.
        
  NodeStore::DummyScheduler scheduler;
  std::unique_ptr <NodeStore::Manager> nodeStoreManager (
    NodeStore::make_Manager ());
        
  std::unique_ptr <NodeStore::Database> nodeStore(
    nodeStoreManager->make_Database("nodeStore",
    scheduler,
    LogPartition::getJournal <NodeObject>(),
    4,
    getConfig().nodeDatabase));

    Status const status (nodeStore->fetch (aNodeObjectHash));

Currently rippled uses only one NodeStore which is created in Application.cpp 
and accessed via the getNodeStore method. 