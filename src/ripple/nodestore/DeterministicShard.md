# Deterministic Database Shards

This doc describes the standard way to assemble the database shard.
A shard assembled using this approach becomes deterministic i.e.
if two independent sides assemble a shard consisting of the same ledgers,
accounts and transactions, then they will obtain the same shard files
`nudb.dat` and `nudb.key`. The approach deals with the `NuDB` database
format only, refer to `https://github.com/vinniefalco/NuDB`.


## Headers

Due to NuDB database definition, the following headers are used for
database files:

nudb.key:
```
char[8]         Type            The characters "nudb.key"
uint16          Version         Holds the version number
uint64          UID             Unique ID generated on creation
uint64          Appnum          Application defined constant
uint16          KeySize         Key size in bytes
uint64          Salt            A random seed
uint64          Pepper          The salt hashed
uint16          BlockSize       Size of a file block in bytes
uint16          LoadFactor      Target fraction in 65536ths
uint8[56]       Reserved        Zeroes
uint8[]         Reserved        Zero-pad to block size
```

nudb.dat:
```
char[8]         Type            The characters "nudb.dat"
uint16          Version         Holds the version number
uint64          UID             Unique ID generated on creation
uint64          Appnum          Application defined constant
uint16          KeySize         Key size in bytes
uint8[64]       (reserved)      Zeroes
```
All of these fields are saved using network byte order
(bigendian: most significant byte first).

To make the shard deterministic the following parameters are used
as values of header field both for `nudb.key` and `nudb.dat` files.
```
Version         2
UID             digest(0)
Appnum          digest(2) | 0x5348524400000000 /* 'SHRD' */
KeySize         32
Salt            digest(1)
Pepper          XXH64(Salt)
BlockSize       0x1000 (4096 bytes)
LoadFactor      0.5 (numeric 0x8000)
```
Note: XXH64() is well-known hash algorithm.

The `digest(i)` mentioned above defined as the follows:

First, RIPEMD160 hash `H` calculated of the following structure
(the same as final Key of the shard):
```
uint32          version         Version of shard, 2 at the present
uint32          firstSeq        Sequence number of first ledger in the shard
uint32          lastSeq         Sequence number of last ledger in the shard
uint256         lastHash        Hash of last ledger in shard
```
there all 32-bit integers are hashed in network byte order
(bigendian: most significant byte first).

Then, `digest(i)` is defined as the following part of the above hash `H`:
```
digest(0) = H[0] << 56 | H[1] << 48 | ... | H[7] << 0,
digest(1) = H[8] << 56 | H[9] << 48 | ... | H[15] << 0,
digest(2) = H[16] << 24 | H[17] << 16 | ... | H[19] << 0,
```
where `H[i]` denotes `i`-th byte of hash `H`.


## Contents

After deterministic shard is created using the above mentioned headers,
it filled with objects using the following steps.

1. All objects within the shard are visited in the order described in the
next section. Here the objects are: ledger headers, SHAmap tree nodes
including state and transaction nodes, final key.

2. Set of all visited objects is divided into groups. Each group except of
the last contains 16384 objects in the order of their visiting. Last group
may contain less than 16384 objects.

3. All objects within each group are sorted in according to their hashes.
Objects are sorted by increasing of their hashes, precisely, by increasing
of hex representations of hashes in lexicographic order. For example,
the following is an example of sorted hashes in their hex representation:
```
0000000000000000000000000000000000000000000000000000000000000000
154F29A919B30F50443A241C466691B046677C923EE7905AB97A4DBE8A5C2429
2231553FC01D37A66C61BBEEACBB8C460994493E5659D118E19A8DDBB1444273
272DCBFD8E4D5D786CF11A5444B30FB35435933B5DE6C660AA46E68CF0F5C441
3C062FD9F0BCDCA31ACEBCD8E530D0BDAD1F1D1257B89C435616506A3EE6CB9E
58A0E5AE427CDDC1C7C06448E8C3E4BF718DE036D827881624B20465C3E1336F
...
```

4. Finally, objects added to the deterministic shard group by group in the
sorted order within each group from low to high hashes.


## Order of visiting objects

The shard consists of 16384 ledgers and the final key with the hash 0.
Each ledger has the header object and two SMAmaps: state and transaction.
SHAmap is a rooted tree in which each node has maximum of 16 descendants
enumerating by indexes 0..15.  Visiting each node in the SHAmap
is performing by functions visitNodes and visitDifferences implemented
in the file `ripple/shamap/impl/ShaMapSync.cpp`.

Here is how the function visitNodes works: it visit the root at first.
Then it visit all nodes in the 1st layer, i. e. the nodes which are
immediately descendants of the root sequentially from index 0 to 15.
Then it visit all nodes in 2nd layer i.e. the nodes which are immediately
descendants the nodes from 1st layer. The order of visiting 2nd layer nodes
is the following. First, descendants of the 1st layer node with index 0
are visited sequintially from index 0 to 15. Then descendents of 1st layer
node with index 1 are visited etc. After visiting all nodes of 2nd layer
the nodes from 3rd layer are visited etc.

The function visitDifferences works similar to visitNodes with the following
exceptions. The first exception is that visitDifferences get 2 arguments:
current SHAmap and previous SHAmap and visit only the nodes from current
SHAmap which and not present in previous SHAmap. The second exception is
that visitDifferences visits all non-leaf nodes in the order of visitNodes
function, but all leaf nodes are visited immedeately after visiting of their
parent node sequentially from index 0 to 15.

Finally, all objects within the shard are visited in the following order.
All ledgers are visited from the ledger with high index to the ledger with
low index in descending order. For each ledger the state SHAmap is visited
first using visitNode function for the ledger with highest index and
visitDifferences function for other ledgers. Then transaction SHAmap is visited
using visitNodes function. At last, the ledger header object is visited.
Final key of the shard is visited at the end.


## Tests

To perform test to deterministic shards implementation one can enter
the following command:
```
rippled --unittest ripple.NodeStore.DatabaseShard
```

The following is the right output of deterministic shards test:
```
ripple.NodeStore.DatabaseShard DatabaseShard deterministic_shard
with backend nudb
Iteration 0: RIPEMD160[nudb.key] = F96BF2722AB2EE009FFAE4A36AAFC4F220E21951
Iteration 0: RIPEMD160[nudb.dat] = FAE6AE84C15968B0419FDFC014931EA12A396C71
Iteration 1: RIPEMD160[nudb.key] = F96BF2722AB2EE009FFAE4A36AAFC4F220E21951
Iteration 1: RIPEMD160[nudb.dat] = FAE6AE84C15968B0419FDFC014931EA12A396C71
```

