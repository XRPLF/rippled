# Installation

## Requirements

Below is an overall list of SOCI core:

* C++ compiler: [GCC](http://gcc.gnu.org/), [Microsoft Visual C++](http://msdn.microsoft.com/en-us/visualc), [LLVM/clang](http://clang.llvm.org/)
* [CMake](http://www.cmake.org) 2.8+ - in order to use build configuration for CMake
* [Boost C++ Libraries](http://www.boost.org): DateTime, Fusion, Optional, Preprocessor, Tuple

and backend-specific dependencies:

* [DB2 Call Level Interface (CLI)](http://pic.dhe.ibm.com/infocenter/db2luw/v10r1/topic/com.ibm.swg.im.dbclient.install.doc/doc/c0023452.html)
* [Firebird client library](http://www.firebirdsql.org/manual/ufb-cs-clientlib.html)
* [mysqlclient](http://dev.mysql.com/doc/refman/5.6/en/c.html) - C API to MySQL
* ODBC (Open Database Connectivity) implementation: [Microsoft ODBC](http://msdn.microsoft.com/en-us/library/windows/desktop/ms710252.aspx) [iODBC](http://www.iodbc.org/), [unixODBC](http://www.unixodbc.org/)
* [Oracle Call Interface (OCI)](http://www.oracle.com/technetwork/database/features/oci/index.html)
* [libpq](http://www.postgresql.org/docs/8.4/static/libpq.html) - C API to PostgreSQL
* [SQLite 3](http://www.sqlite.org/) library

## Downloads

Download package with latest release of the SOCI source code: [soci-X.Y.Z](https://sourceforge.net/projects/soci/), where X.Y.Z is the version number.
Unpack the archive.

You can always clone SOCI from the Git repository:

```console
git clone git://github.com/SOCI/soci.git
```

## Building with CMake

SOCI is configured to build using [CMake](http://cmake.org/) system in version 2.8+.

The build configuration allows to control various aspects of compilation and installation by setting common CMake variables that change behaviour, describe system or control build (see [CMake help](http://cmake.org/cmake/help/documentation.html)) as well as SOCI-specific variables described below.
All these variables are available regardless of platform or compilation toolset used.

Running CMake from the command line allows to set variables in the CMake cache with the following syntax: `-DVARIABLE:TYPE=VALUE`.
If you are new to CMake, you may find the tutorial [Running CMake](http://cmake.org/cmake/help/runningcmake.html") helpful.

### Running CMake on Unix

Steps outline using GNU Make makefiles:

```console
mkdir build
cd build
cmake -G "Unix Makefiles" -DWITH_BOOST=OFF -DWITH_ORACLE=OFF (...) /path/to/soci-X.Y.Z
make
make install
```

### Running CMake on Windows

Steps outline using Visual Studio 2010 and MSBuild:

```console
mkdir build
cd build
cmake -G "Visual Studio 10" -DWITH_BOOST=OFF -DWITH_ORACLE=OFF (...) C:\path\to\soci-X.Y.Z
msbuild.exe SOCI.sln
```

### CMake configuration

By default, CMake will try to determine availability of all depdendencies automatically.
If you are lucky, you will not need to specify any of the CMake variables explained below.
However, if CMake reports some of the core or backend-specific dependencies as missing, you will need specify relevant variables to tell CMake where to look for the required components.

CMake configures SOCI build performing sequence of steps.
Each subsequent step is dependant on result of previous steps corresponding with particular feature.
First, CMake checks system platform and compilation toolset.
Next, CMake tries to find all external dependencies.
Then, depending on the results of the dependency check, CMake determines SOCI backends which are possible to build.
The SOCI-specific variables described below provide users with basic control of this behaviour.

The following sections provide summary of variables accepted by CMake scripts configuring SOCI build.
The lists consist of common variables for SOCI core and all backends as well as variables specific to SOCI backends and their direct dependencies.

List of a few essential CMake variables:

* `CMAKE_BUILD_TYPE` - string - Specifies the build type for make based generators (see CMake [help](http://cmake.org/cmake/help/cmake-2-8-docs.html#variable:CMAKE_BUILD_TYPE)).
* `CMAKE_INSTALL_PREFIX` - path - Install directory used by install command (see CMake [help](http://cmake.org/cmake/help/cmake-2-8-docs.html#variable:CMAKE_INSTALL_PREFIX)).
* `CMAKE_VERBOSE_MAKEFILE` - boolean - If ON, create verbose makefile (see CMake [help](http://cmake.org/cmake/help/cmake-2-8-docs.html#variable:CMAKE_VERBOSE_MAKEFILE)).

List of variables to control common SOCI features and dependencies:

* `SOCI_STATIC` - boolean - Request to build static libraries, along with shared, of SOCI core and all successfully configured backends.
* `SOCI_TESTS` - boolean - Request to build regression tests for SOCI core and all successfully configured backends.
* `WITH_BOOST` - boolean - Should CMake try to detect [Boost C++ Libraries](http://www.boost.org/). If ON, CMake will try to find Boost headers and binaries of [Boost.Date_Time](http://www.boost.org/doc/libs/release/doc/html/date_time.html) library.

#### Empty (sample backend)

* `SOCI_EMPTY` - boolean - Builds the [sample backend](backends.html) called Empty. Always ON by default.
* `SOCI_EMPTY_TEST_CONNSTR` - string - Connection string used to run regression tests of the Empty backend. It is a dummy value. Example: `-DSOCI_EMPTY_TEST_CONNSTR="dummy connection"`

#### IBM DB2

* `WITH_DB2` - boolean - Should CMake try to detect IBM DB2 Call Level Interface (CLI) library.
* `DB2_INCLUDE_DIR` - string - Path to DB2 CLI include directories where CMake should look for `sqlcli1.h` header.
* `DB2_LIBRARIES` - string - Full paths to  `db2` or `db2api` libraries to link SOCI against to enable the backend support.
* `SOCI_DB2` - boolean - Requests to build [DB2](backends/db2.html) backend. Automatically switched on, if `WITH_DB2` is set to ON.
* `SOCI_DB2_TEST_CONNSTR` - string - See [DB2 backend reference](backends/db2.html) for details. Example: `-DSOCI_DB2_TEST_CONNSTR:STRING="DSN=SAMPLE;Uid=db2inst1;Pwd=db2inst1;autocommit=off"`

#### Firebird

* `WITH_FIREBIRD` - boolean - Should CMake try to detect Firebird client library.
* `FIREBIRD_INCLUDE_DIR` - string - Path to Firebird include directories where CMake should look for `ibase.h` header.
* `FIREBIRD_LIBRARIES` - string - Full paths to Firebird `fbclient` or `fbclient_ms` libraries to link SOCI against to enable the backend support.
* `SOCI_FIREBIRD` - boolean - Requests to build [Firebird](backends/firebird.html) backend. Automatically switched on, if `WITH_FIREBIRD` is set to ON.
* `SOCI_FIREBIRD_TEST_CONNSTR` - string - See [Firebird backend refernece](backends/firebird.html) for details. Example: `-DSOCI_FIREBIRD_TEST_CONNSTR:STRING="service=LOCALHOST:/tmp/soci_test.fdb user=SYSDBA password=masterkey"`

#### MySQL

* `WITH_MYSQL` - boolean - Should CMake try to detect [mysqlclient](http://dev.mysql.com/doc/refman/5.0/en/c.html) libraries providing MySQL C API. Note, currently the [mysql_config](http://dev.mysql.com/doc/refman/5.0/en/building-clients.html) program is not being used.
* `MYSQL_DIR` - string - Path to MySQL installation root directory. CMake will scan subdirectories `MYSQL_DIR/include` and `MYSQL_DIR/lib` respectively for MySQL headers and libraries.
* `MYSQL_INCLUDE_DIR` - string - Path to MySQL include directory where CMake should look for `mysql.h` header.
* `MYSQL_LIBRARIES` - string - Full paths to libraries to link SOCI against to enable the backend support.
* `SOCI_MYSQL` - boolean - Requests to build [MySQL](backends/mysql.html) backend. Automatically switched on, if `WITH_MYSQL` is set to ON.
* `SOCI_MYSQL_TEST_CONNSTR` - string - Connection string to MySQL test database. Format of the string is explained [MySQL backend refernece](backends/mysql.html). Example: `-DSOCI_MYSQL_TEST_CONNSTR:STRING="db=mydb user=mloskot password=secret"`

#### ODBC

* `WITH_ODBC` - boolean - Should CMake try to detect ODBC libraries. On Unix systems, CMake tries to find [unixODBC](http://www.unixodbc.org/) or [iODBC](http://www.iodbc.org/) implementations.
* `ODBC_INCLUDE_DIR` - string - Path to ODBC implementation include directories where CMake should look for `sql.h` header.
* `ODBC_LIBRARIES` - string - Full paths to libraries to link SOCI against to enable the backend support.
* `SOCI_ODBC` - boolean - Requests to build [ODBC](backends/odbc.html) backend. Automatically switched on, if `WITH_ODBC` is set to ON.
* `SOCI_ODBC_TEST_{database}_CONNSTR` - string - ODBC Data Source Name (DSN) or ODBC File Data Source Name (FILEDSN) to test database: Microsoft Access (.mdb), Microsoft SQL Server, MySQL, PostgreSQL or any other ODBC SQL data source. {database} is placeholder for name of database driver ACCESS, MYSQL, POSTGRESQL, etc. See [ODBC](backends/odbc.html) backend refernece for details. Example: `-DSOCI_ODBC_TEST_POSTGRESQL_CONNSTR="FILEDSN=/home/mloskot/soci/build/test-postgresql.dsn"`

#### Oracle

* `WITH_ORACLE` - boolean - Should CMake try to detect [Oracle Call Interface (OCI)](http://en.wikipedia.org/wiki/Oracle_Call_Interface) libraries.
* `ORACLE_INCLUDE_DIR` - string - Path to Oracle include directory where CMake should look for `oci.h` header.
* `ORACLE_LIBRARIES` - string - Full paths to libraries to link SOCI against to enable the backend support.
* `SOCI_ORACLE` - boolean - Requests to build [Oracle](backends/oracle.html) backend. Automatically switched on, if `WITH_ORACLE` is set to ON.
* `SOCI_ORACLE_TEST_CONNSTR` - string - Connection string to Oracle test database. Format of the string is explained [Oracle backend reference](backends/oracle.html). Example: `-DSOCI_ORACLE_TEST_CONNSTR:STRING="service=orcl user=scott password=tiger"`

#### PostgreSQL

* `WITH_POSTGRESQL` - boolean - Should CMake try to detect PostgreSQL client interface libraries. SOCI relies on [libpq](http://www.postgresql.org/docs/9.0/interactive/libpq.html") C library.
* `POSTGRESQL_INCLUDE_DIR` - string - Path to PostgreSQL include directory where CMake should look for `libpq-fe.h` header.
* `POSTGRESQL_LIBRARIES` - string - Full paths to libraries to link SOCI against to enable the backend support.
* `SOCI_POSTGRESQL` - boolean - Requests to build [PostgreSQL](backends/postgresql.html") backend. Automatically switched on, if `WITH_POSTGRESQL` is set to ON.
* `SOCI_POSTGRESQL_TEST_CONNSTR` - string - Connection string to PostgreSQL test database. Format of the string is explained PostgreSQL backend refernece. Example: `-DSOCI_POSTGRESQL_TEST_CONNSTR:STRING="dbname=mydb user=scott"`

#### SQLite 3

* `WITH_SQLITE3` - boolean - Should CMak try to detect SQLite C/C++ library. As bonus, the configuration tries OSGeo4W distribution if OSGEO4W_ROOT environment variable is set.
* `SQLITE_INCLUDE_DIR` - string - Path to SQLite 3 include directory where CMake should look for `sqlite3.h` header.
* `SQLITE_LIBRARIES` - string - Full paths to libraries to link SOCI against to enable the backend support.
* `SOCI_SQLITE3` - boolean - Requests to build [SQLite3](backends/sqlite3.html) backend. Automatically switched on, if `WITH_SQLITE3` is set to ON.
* `SOCI_SQLITE3_TEST_CONNSTR` - string - Connection string is simply a file path where SQLite3 test database will be created (e.g. /home/john/soci_test.db). Check [SQLite3 backend reference](backends/sqlite3.html) for details. Example: `-DSOCI_SQLITE3_TEST_CONNSTR="my.db"` or `-DSOCI_SQLITE3_TEST_CONNSTR=":memory:"`.

## Building with Makefiles on Unix

*NOTE: These (classic) Makefiles have not been maintained for long time.
The officially maintained build configuration is CMake.
If you still want to use these Makefiles, you've been warned that you may need to patch them.*

The classic set of Makefiles for Unix/Linux systems is provided for those users who need complete control over the whole processand who can benefit from the basic scaffolding that they can extend on their own.
In this sense, the basic Makefiles are supposed to provide a minimal starting point for custom experimentation and are not intended to be a complete build/installation solution.
At the same time, they are complete in the sense that they can compile the library with all test programs and for some users this level of support will be just fine.

The `core` directory of the library distribution contains the `Makefile.basic` that can be used to compile the core part of the library.
Run `make -f Makefile.basic` or `make -f Makefile.basic shared` to get the static and shared versions, respectively.
Similarly, the `backends/<i>name</i>` directory contains the backend part for each supported backend with the appropriate `Makefile.basic` and the `backends/<i>name</i>/test` directory contains the test program for the given backend.

For example, the simplest way to compile the static version of the library and the test program for PostgreSQL is:

```console
cd src/core
make -f Makefile.basic
cd ../backends/postgresql
make -f Makefile.basic
cd test
make -f Makefile.basic
```

For each backend and its test program, the `Makefile.basic`s contain the variables that can have values specific to the given environment - they usually name the include and library paths.
These variables are placed at the beginning of the `Makefile.basic`s.
Please review their values in case of any compilation problems.

The Makefiles for test programs can be a good starting point to find out correct compiler and linker options.

## Running tests

The process of running regression tests highly depends on user's environment and build configuration, so it may be quite involving process.
The CMake configuration provides variables to allow users willing to run the tests to configure build and specify database connection parameters (see the lists above for variable names).

In order to run regression tests, configure and build desired SOCI backends and prepare working database instances for them.

While configuring build with CMake, specify `SOCI_TESTS=ON` to enable building regression tests.
Also, specify `SOCI_{backend name}_TEST_CONNSTR` variables to tell the tests runner how to connect with your test databases.

Dedicated `make test` target can be used to execute regression tests on build completion:

```console
mkdir build
cd build
cmake -G "Unix Makefiles" \
        -DWITH_BOOST=OFF \
        -DSOCI_TESTS=ON \
        -DSOCI_EMPTY_TEST_CONNSTR="dummy connection" \
        -DSOCI_SQLITE3_TEST_CONNSTR="test.db" \
        (...)
        ../soci-X.Y.Z
make
make test
make install
```

In the example above, regression tests for the sample Empty backend and SQLite 3 backend are configured for execution by `make test` target.

## Using library

CMake build produces set of shared and static libraries for SOCI core and backends separately.
On Unix, for example, `build/lib` directory will consist of the static libraries named like `libsoci_core.a`, `libsoci_sqlite3.a` and shared libraries with names like `libsoci_core.so.4.0.0`, `libsoci_sqlite3.so.4.0.0`, and so on.

In order to use SOCI in your program, you need to specify your project build configuration with paths to SOCI headers and libraries.
Then, tell the linker to link against the libraries you want to use in your program.
