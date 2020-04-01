# Deterministic Database Shards

This doc describes the standard way to assemble the database shard. A shard assembled using this approach becomes deterministic i.e. if two independent sides assemble the shard consists of the same ledgers, accounts and transactions, then they will obtain the same shard files `nudb.dat` and `nudb.key`. The approach deals with the `NuDB` database format only, refer to `https://github.com/vinniefalco/NuDB`.


## Headers

Due to NuDB database definition, the following headers are used for database files:

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
there all fields are saved using network byte order (most significant byte first).

To make the shard deterministic the following parameters are used as values of header field both for `nudb.key` and `nudb.dat` files.
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
```
uint256         lastHash        Hash of last ledger in shard
uint32          index           Index of the shard
uint32          firstSeq        Sequence number of first ledger in the shard
uint32          lastSeq         Sequence number of last ledger in the shard
uint32          version         Version of shard, 2 at the present
```
there all 32-bit integers are hashed in network byte order.

Then, `digest(i)` is defined as the following portion of the above hash `H`:
```
digest(0) = H[0] << 56 | H[2] << 48 | ... | H[14] << 0,
digest(1) = H[1] << 56 | H[3] << 48 | ... | H[15] << 0,
digest(2) = H[19] << 24 | H[18] << 16 | ... | H[16] << 0,
```
where `H[i]` denotes `i`-th byte of hash `H`.


## Contents

After deterministic shard is created using the above mentioned headers, it filled with objects. First, all objects of the shard are collected and sorted in according to their hashes. Here the objects are: ledgers, SHAmap tree nodes including accounts and transactions, and final key object with hash 0. Objects are sorted by increasing of their hashes, precisely, by increasing of hex representations of hashes in lexicographic order. 

For example, the following is an example of sorted hashes in their hex representation:
```
0000000000000000000000000000000000000000000000000000000000000000
154F29A919B30F50443A241C466691B046677C923EE7905AB97A4DBE8A5C2423
2231553FC01D37A66C61BBEEACBB8C460994493E5659D118E19A8DDBB1444273
272DCBFD8E4D5D786CF11A5444B30FB35435933B5DE6C660AA46E68CF0F5C447
3C062FD9F0BCDCA31ACEBCD8E530D0BDAD1F1D1257B89C435616506A3EE6CB9E
58A0E5AE427CDDC1C7C06448E8C3E4BF718DE036D827881624B20465C3E1334F
...
```

Finally, objects added to the shard one by one in the sorted order from low to high hashes.


## Tests

To perform test to deterministic shards implementation one can enter the following command:
```
rippled --unittest ripple.NodeStore.DatabaseShard
```

The following is the right output of deterministic shards test:
```
ripple.NodeStore.DatabaseShard DatabaseShard deterministic_shard with backend nudb
Iteration 0: RIPEMD160[nudb.key] = 4CFA8985836B549EC99D2E9705707F488DC91E4E
Iteration 0: RIPEMD160[nudb.dat] = 8CC61F503C36339803F8C2FC652C1102DDB889F1
Iteration 1: RIPEMD160[nudb.key] = 4CFA8985836B549EC99D2E9705707F488DC91E4E
Iteration 1: RIPEMD160[nudb.dat] = 8CC61F503C36339803F8C2FC652C1102DDB889F1
```

