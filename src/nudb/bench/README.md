# Benchmarks for NuDB

These benchmarks time two operations:

1. The time to insert N values into a database. The inserted keys and values are
   pseudo-randomly generated. The random number generator is always seeded with
   the same value for each run, so the same values are always inserted.
2. The time to fetch M existing values from a database with N values. The order
   that the keys are fetched are pseudo-randomly generated. The random number
   generator is always seeded with the same value on each fun, so the keys are
   always looked up in the same order.

At the end of a run, the program outputs a table of operations per second. The
tables have a row for each database size, and a column for each database (in
cases where NuDB is compared against other databases). A cell in the table is
the number of operations per second for that trial. For example, in the table
below NuDB had 340397 Ops/Sec when fetching from an existing database with
10,000,000 values. This is a summary report, and only reports samples at order
of magnitudes of ten.

A sample output:

```
insert (per second)
    num_db_keys          nudb       rocksdb
         100000        406598        231937
        1000000        374330        258519
       10000000            NA            NA

fetch (per second)
    num_db_keys          nudb       rocksdb
         100000        325228        697158
        1000000        333443         34557
       10000000        337300         20835
```

In addition to the summary report, the benchmark can collect detailed samples.
The `--raw_out` command line options is used to specify a file to output the raw
samples. The python 3 script `plot_bench.py` may be used to plot the result. For
example, if bench was run as `bench --raw_out=samples.txt`, the the python
script can be run as `python plot_bench.py -i samples.txt`. The python script
requires the `pandas` and `seaborn` packages (anaconda python is a good way to
install and manage python if these packages are not already
installed: [anaconda download](https://www.continuum.io/downloads)).

# Building

## Building with CMake

Note: Building with RocksDB is currently not supported on Windows.

1. The benchmark requires boost. If building with rocksdb, it also requires zlib
   and snappy. These are popular libraries and should be available through the
   package manager.
1. The benchmark and test programs require some submodules that are not
   installed by default. Get these submodules by running:
   `git submodule update --init`
2. From the main nudb directory, create a directory for the build and change to
   that directory: `mkdir bench_build;cd bench_build`
3. Generate a project file or makefile.
   * If building on Linux, generate a makefile. If building with rocksdb
   support, use: `cmake -DCMAKE_BUILD_TYPE=Release ../bench` If building
   without rocksdb support, use: `cmake -DCMAKE_BUILD_TYPE=Release ../bench
   -DWITH_ROCKSDB=false` Replace `../bench` with the path to the `bench`
   directory if the build directory is not in the suggested location.
   * If building on windows, generate a project file. The CMake gui program is
   useful for this. Use the `bench` directory as the `source` directory and
   the `bench_build` directory as the `binaries` directory. Press the `Add
   Entry` button and add a `BOOST_ROOT` variable that points to the `boost`
   directory. Hit `configure`. A dialog box will pop up. Select the generator
   for Win64. Select `generate` to generate the visual studio project.
4. Compile the program.
   * If building on Linux, run: `make`
   * If building on Windows, open the project file generated above in Visual
   Studio.

## Test the build

Try running the benchmark with a small database: `./bench --num_batches=10`. A
report similar to sample should appear after a few seconds.

# Command Line Options

* `batch_size arg` : Number of elements to insert or fetch per batch. If not
  specified, it defaults to 20000.
* `num_batches arg` : Number of batches to run. If not specified, it defaults to
  500.
* `db_dir arg` : Directory to place the databases. If not specified, it defaults to
  boost::filesystem::temp_directory_path (likely `/tmp` on Linux)
* `raw_out arg` : File to record the raw measurements. This is useful for plotting. If
  not specified the raw measurements will not be output.
*  `--dbs arg` : Databases to run the benchmark on. Currently, only `nudb` and
   `rocksdb` are supported. Building with `rocksdb` is optional on Linux, and
   only `nudb` is supported on windows. The argument may be a list. If `dbs` is
   not specified, it defaults to all the database the build supports (either
   `nudb` or `nudb rocksdb`).
*  `--key_size arg` : nudb key size. If not specified the default is 64.
*  `--block_size arg` : nudb block size. This is an advanced argument. If not
   specified the default is 4096.
*  `--load_factor arg` : nudb load factor. This is an advanced argument. If not
   specified the default is 0.5.

