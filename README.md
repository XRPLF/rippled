<img width="880" height = "80" alt = "NuDB"
    src="https://raw.githubusercontent.com/vinniefalco/NuDB/master/doc/images/readme2.png">

[![Join the chat at https://gitter.im/vinniefalco/NuDB](https://badges.gitter.im/vinniefalco/NuDB.svg)](https://gitter.im/vinniefalco/NuDB?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge) [![Build Status]
(https://travis-ci.org/vinniefalco/NuDB.svg?branch=master)](https://travis-ci.org/vinniefalco/NuDB) [![codecov]
(https://codecov.io/gh/vinniefalco/NuDB/branch/master/graph/badge.svg)](https://codecov.io/gh/vinniefalco/NuDB) [![coveralls]
(https://coveralls.io/repos/github/vinniefalco/NuDB/badge.svg?branch=master)](https://coveralls.io/github/vinniefalco/NuDB?branch=master) [![Documentation]
(https://img.shields.io/badge/documentation-master-brightgreen.svg)](http://vinniefalco.github.io/nudb/) [![License]
(https://img.shields.io/badge/license-boost-brightgreen.svg)](LICENSE_1_0.txt)

# A Key/Value Store For SSDs

---

## Contents

- [Introduction](#introduction)
- [Description](#description)
- [Requirements](#requirements)
- [Example](#example)
- [Building](#building)
- [Algorithm](#algorithm)
- [Licence](#licence)
- [Contact](#contact)

---

## Introduction

NuDB is an append-only, key/value store specifically optimized for random
read performance on modern SSDs or equivalent high-IOPS devices. The most
common application for NuDB is content addressible storage where a
cryptographic digest of the data is used as the key. The read performance
and memory usage are independent of the size of the database. These are
some other features:

* Low memory footprint
* Database size up to 281TB
* All keys are the same size
* Append-only, no update or delete
* Value sizes from 1 to 2^32 bytes (4GB)
* Performance independent of growth
* Optimized for concurrent fetch
* Key file can be rebuilt if needed
* Inserts are atomic and consistent
* Data file may be efficiently iterated
* Key and data files may be on different devices
* Hardened against algorithmic complexity attacks
* Header-only, no separate library to build

## Description

This software is close to final. Interfaces are stable.
For recent changes see the [CHANGELOG](CHANGELOG.md).

NuDB has been in use for over a year on production servers
running [rippled](https://github.com/ripple/rippled), with
database sizes over 3 terabytes.

* [Repository](https://github.com/vinniefalco/Beast)
* [Documentation](http://vinniefalco.github.io/nudb/)

## Requirements

* Boost 1.58 or higher
* C++11 or greater
* SSD drive, or equivalent device with high IOPS

## Example

This complete program creates a database, opens the database,
inserts several key/value pairs, fetches the key/value pairs,
closes the database, then erases the database files. Source
code for this program is located in the examples directory.

```C++
#include <nudb/nudb.hpp>
#include <cstddef>
#include <cstdint>

int main()
{
    using namespace nudb;
    std::size_t constexpr N = 1000;
    using key_type = std::uint32_t;
    error_code ec;
    auto const dat_path = "db.dat";
    auto const key_path = "db.key";
    auto const log_path = "db.log";
    create<xxhasher>(
        dat_path, key_path, log_path,
        1,
        make_salt(),
        sizeof(key_type),
        block_size("."),
        0.5f,
        ec);
    store db;
    db.open(dat_path, key_path, log_path, ec);
    char data = 0;
    // Insert
    for(key_type i = 0; i < N; ++i)
        db.insert(&i, &data, sizeof(data), ec);
    // Fetch
    for(key_type i = 0; i < N; ++i)
        db.fetch(&i,
            [&](void const* buffer, std::size_t size)
        {
            // do something with buffer, size
        }, ec);
    db.close(ec);
    erase_file(dat_path);
    erase_file(key_path);
    erase_file(log_path);
}
```

## Building

NuDB is header-only so there are no libraries to build. To use it in your
project, simply copy the NuDB sources to your project's source tree
(alternatively, bring NuDB into your Git repository using the
`git subtree` or `git submodule` commands). Then, edit your build scripts
to add the `include/` directory to the list of paths checked by the C++
compiler when searching for includes. NuDB `#include` lines will look
like this:

```
#include <nudb/nudb.hpp>
```

To link your program successfully, you'll need to add the Boost.Thread and
Boost.System libraries to link with. Please visit the Boost documentation
for instructions on how to do this for your particular build system.

NuDB tests require Beast, and the benchmarks require RocksDB. These projects
are linked to the repository using git submodules. Before building the tests
or benchmarks, these commands should be issued at the root of the repository:

```
git submodule init
git submodule update
```

For the examples and tests, NuDB provides build scripts for Boost.Build (b2)
and CMake. To generate build scripts using CMake, execute these commands at
the root of the repository (project and solution files will be generated
for Visual Studio users):

```
cd bin
cmake ..                                    # for 32-bit Windows build

cd ../bin64
cmake ..                                    # for Linux/Mac builds, OR
cmake -G"Visual Studio 14 2015 Win64" ..    # for 64-bit Windows builds
```

To build with Boost.Build, it is necessary to have the b2 executable
in your path. And b2 needs to know how to find the Boost sources. The
easiest way to do this is make sure that the version of b2 in your path
is the one at the root of the Boost source tree, which is built when
running `bootstrap.sh` (or `bootstrap.bat` on Windows).

Once b2 is in your path, simply run b2 in the root of the Beast
repository to automatically build the required Boost libraries if they
are not already built, build the examples, then build and run the unit
tests.

On OSX it may be necessary to pass "toolset=clang" on the b2 command line.
Alternatively, this may be site in site-config.jam or user-config.jam.

The files in the repository are laid out thusly:

```
./
    bench/          Holds the benchmark sources and scripts
    bin/            Holds executables and project files
    bin64/          Holds 64-bit Windows executables and project files
    examples/       Holds example program source code
    extras/         Additional APIs, may change
    include/        Add this to your compiler includes
        nudb/
    test/           Unit tests and benchmarks
    tools/          Holds the command line tool sources
```

## Algorithm

Three files are used.

* The data file holds keys and values stored sequentially and size-prefixed.
* The key file holds a series of fixed-size bucket records forming an on-disk
  hash table.
* The log file stores bookkeeping information used to restore consistency when
an external failure occurs.

In typical cases a fetch costs one I/O cycle to consult the key file, and if the
key is present, one I/O cycle to read the value.

### Usage

Callers must define these parameters when _creating_ a database:

* `KeySize`: The size of a key in bytes.
* `BlockSize`: The physical size of a key file record.

The ideal block size matches the sector size or block size of the
underlying physical media that holds the key file. Functions are
provided to return a best estimate of this value for a particular
device, but a default of 4096 should work for typical installations.
The implementation tries to fit as many entries as possible in a key
file record, to maximize the amount of useful work performed per I/O.

* `LoadFactor`: The desired fraction of bucket occupancy

`LoadFactor` is chosen to make bucket overflows unlikely without
sacrificing bucket occupancy. A value of 0.50 seems to work well with
a good hash function.

Callers must also provide these parameters when a database is _opened:_

* `Appnum`: An application-defined integer constant which can be retrieved
later from the database [TODO].
* `AllocSize`: A significant multiple of the average data size.

Memory is recycled to improve performance, so NuDB needs `AllocSize` as a
hint about the average size of the data being inserted. For an average data size
of 1KB (one kilobyte), `AllocSize` of sixteen megabytes (16MB) is sufficient. If
the `AllocSize` is too low, the memory recycler will not make efficient use of
allocated blocks.

Two operations are defined: `fetch`, and `insert`.

#### `fetch`

The `fetch` operation retrieves a variable length value given the
key. The caller supplies a factory used to provide a buffer for storing
the value. This interface allows custom memory allocation strategies.

#### `insert`

`insert` adds a key/value pair to the store. Value data must contain at least
one byte. Duplicate keys are disallowed. Insertions are serialized, which means
[TODO].

### Implementation

All insertions are buffered in memory, with inserted values becoming
immediately discoverable in subsequent or concurrent calls to fetch.
Periodically, buffered data is safely committed to disk files using
a separate dedicated thread associated with the database. This commit
process takes place at least once per second, or more often during
a detected surge in insertion activity. In the commit process the
key/value pairs receive the following treatment:

An insertion is performed by appending a value record to the data file.
The value record has some header information including the size of the
data and a copy of the key; the data file is iteratable without the key
file. The value data follows the header. The data file is append-only
and immutable: once written, bytes are never changed.

Initially the hash table in the key file consists of a single bucket.
After the load factor is exceeded from insertions, the hash table grows
in size by one bucket by doing a "split". The split operation is the
[linear hashing algorithm](http://en.wikipedia.org/wiki/Linear_hashing)
as described by Litwin and Larson.

When a bucket is split, each key is rehashed, and either remains in the
original bucket or gets moved to the a bucket appended to the end of
the key file.

An insertion on a full bucket first triggers the "spill" algorithm.

First, a spill record is appended to the data file, containing header
information followed by the entire bucket record. Then the bucket's size is set
to zero and the offset of the spill record is stored in the bucket. At this
point the insertion may proceed normally, since the bucket is empty. Spilled
buckets in the data file are always full.

Because every bucket holds the offset of the next spill record in the
data file, the buckets form a linked list. In practice, careful
selection of capacity and load factor will keep the percentage of
buckets with one spill record to a minimum, with no bucket requiring
two spill records.

The implementation of fetch is straightforward: first the bucket in the
key file is checked, then each spill record in the linked list of
spill records is checked, until the key is found or there are no more
records. As almost all buckets have no spill records, the average
fetch requires one I/O (not including reading the value).

One complication in the scheme is when a split occurs on a bucket that
has one or more spill records. In this case, both the bucket being split
and the new bucket may overflow. This is handled by performing the
spill algorithm for each overflow that occurs. The new buckets may have
one or more spill records each, depending on the number of keys that
were originally present.

Because the data file is immutable, a bucket's original spill records
are no longer referenced after the bucket is split. These blocks of data
in the data file are unrecoverable wasted space. Correctly configured
databases can have a typical waste factor of 1%, which is acceptable.
These unused bytes can be removed by visiting each value in the value
file using an off-line process and inserting it into a new database,
then delete the old database and use the new one instead.

### Recovery

To provide atomicity and consistency, a log file associated with the
database stores information used to roll back partial commits.

### Iteration

Each record in the data file is prefixed with a header identifying
whether it is a value record or a spill record, along with the size of
the record in bytes and a copy of the key if it's a value record, so values can
be iterated by incrementing a byte counter. A key file can be regenerated from
just the data file by iterating the values and performing the key
insertion algorithm.

### Concurrency

Locks are never held during disk reads and writes. Fetches are fully
concurrent, while inserts are serialized. Inserts fail on duplicate
keys, and are atomic: they either succeed immediately or fail.
After an insert, the key is immediately visible to subsequent fetches.

### Formats

All integer values are stored as big endian. The uint48_t format
consists of 6 bytes.

#### Key File

The Key File contains the Header followed by one or more
fixed-length Bucket Records.

#### Header (104 bytes)

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

`Type` identifies the file as belonging to nudb. `UID` is
generated randomly when the database is created, and this value
is stored in the data and log files as well - it's used
to determine if files belong to the same database. `Salt` is
generated when the database is created and helps prevent
complexity attacks; it is prepended to the key material
when computing a hash, or used to initialize the state of
the hash function. `Appnum` is an application defined constant
set when the database is created. It can be used for anything,
for example to distinguish between different data formats.

`Pepper` is computed by hashing `Salt` using a hash function
seeded with the salt. This is used to fingerprint the hash
function used. If a database is opened and the fingerprint
does not match the hash calculation performed using the template
argument provided when constructing the store, an exception
is thrown.

The header for the key file contains the File Header followed by
the information above. The Capacity is the number of keys per
bucket, and defines the size of a bucket record. The load factor
is the target fraction of bucket occupancy.

None of the information in the key file header or the data file
header may be changed after the database is created, including
the Appnum.

#### Bucket Record (fixed-length)

    uint16              Count           Number of keys in this bucket
    uint48              Spill           Offset of the next spill record or 0
    BucketEntry[]       Entries         The bucket entries

#### Bucket Entry

    uint48              Offset          Offset in data file of the data
    uint48              Size            The size of the value in bytes
    uint48              Hash            The hash of the key

### Data File

The Data File contains the Header followed by zero or more
variable-length Value Records and Spill Records.

#### Header (92 bytes)

    char[8]             Type            The characters "nudb.dat"
    uint16              Version         Holds the version number
    uint64              UID             Unique ID generated on creation
    uint64              Appnum          Application defined constant
    uint16              KeySize         Key size in bytes
    uint8[64]           (reserved)      Zeroes

UID contains the same value as the salt in the corresponding key
file. This is placed in the data file so that key and value files
belonging to the same database can be identified.

#### Data Record (variable-length)

    uint48              Size            Size of the value in bytes
    uint8[KeySize]      Key             The key.
    uint8[Size]         Data            The value data.

#### Spill Record (fixed-length)

    uint48              Zero            All zero, identifies a spill record
    uint16              Size            Bytes in spill bucket (for skipping)
    Bucket              SpillBucket     Bucket Record

#### Log File

The Log file contains the Header followed by zero or more fixed size
log records. Each log record contains a snapshot of a bucket. When a
database is not closed cleanly, the recovery process applies the log
records to the key file, overwriting data that may be only partially
updated with known good information. After the log records are applied,
the data and key files are truncated to the last known good size.

#### Header (62 bytes)

    char[8]             Type            The characters "nudb.log"
    uint16              Version         Holds the version number
    uint64              UID             Unique ID generated on creation
    uint64              Appnum          Application defined constant
    uint16              KeySize         Key size in bytes

    uint64              Salt            A random seed.
    uint64              Pepper          The salt hashed
    uint16              BlockSize       Size of a file block in bytes

    uint64              KeyFileSize     Size of key file.
    uint64              DataFileSize    Size of data file.

#### Log Record

    uint64_t            Index           Bucket index (0-based)
    Bucket              Bucket          Compact Bucket record

Compact buckets include only Size entries. These are primarily
used to minimize the volume of writes to the log file.

## License

Distributed under the Boost Software License, Version 1.0.
(See accompanying file [LICENSE_1_0.txt](LICENSE_1_0.txt) or copy at
http://www.boost.org/LICENSE_1_0.txt)

## Contact

Please report issues or questions here:
https://github.com/vinniefalco/NuDB/issues
