
# NodeStore

## Introduction

The NodeStore provides an interface that stores, in a persistent database, the collection of
NodeObject that rippled uses as its primary representation of ledger items.

## Module directory structure

nodestore
|-api // Public Interface 
|
|-backend // Factory classes for various databases 
|
|-impl // Private Implementation
|
|-test // Unit tests

The NodeStore class is a simple object that the Ledger uses to store entries. It has a enumeration type, a hash, a ledger index and a Blob which stores arbritary data.


# Document WIP notes

If the MemoryFactory backend database is used, do we loose persistance?