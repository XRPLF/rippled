# PostgreSQL Backend Reference

SOCI backend for accessing PostgreSQL database.

## Prerequisites

### Supported Versions

The SOCI PostgreSQL backend is supported for use with PostgreSQL >= 7.3, although versions older than 8.0 will suffer from limited feature support. See below for details.

### Tested Platforms

|PostgreSQL|OS|Compiler|
|--- |--- |--- |
|9.6|Windows Server 2016|MSVC++ 14.1|
|9.4|Windows Server 2012 R2|MSVC++ 14.0|
|9.4|Windows Server 2012 R2|MSVC++ 12.0|
|9.4|Windows Server 2012 R2|MSVC++ 11.0|
|9.4|Windows Server 2012 R2|Mingw-w64/GCC 4.8|
|9.3|Ubuntu 12.04|g++ 4.6.3|
|9.0|Mac OS X 10.6.6|g++ 4.2|
|8.4|FreeBSD 8.2|g++ 4.1|
|8.4|Debian 6|g++ 4.3|
|8.4|RedHat 5|g++ 4.3|

### Required Client Libraries

The SOCI PostgreSQL backend requires PostgreSQL's `libpq` client library.

Note that the SOCI library itself depends also on `libdl`, so the minimum set of libraries needed to compile a basic client program is:

```console
-lsoci_core -lsoci_postgresql -ldl -lpq
```

### Connecting to the Database

To establish a connection to the PostgreSQL database, create a `session` object using the `postgresql` backend factory together with a connection string:

```cpp
session sql(postgresql, "dbname=mydatabase");

// or:
session sql("postgresql", "dbname=mydatabase");

// or:
session sql("postgresql://dbname=mydatabase");
```

The set of parameters used in the connection string for PostgreSQL is the same as accepted by the `[PQconnectdb](http://www.postgresql.org/docs/8.3/interactive/libpq.html#LIBPQ-CONNECT)` function from the `libpq` library.

In addition to standard PostgreSQL connection parameters, the following can be set:

* `singlerow` or `singlerows`

For example:

```cpp
session sql(postgresql, "dbname=mydatabase singlerows=true");
```

If the `singlerows` parameter is set to `true` or `yes`, then queries will be executed in the single-row mode, which prevents the client library from loading full query result set into memory and instead fetches rows one by one, as they are requested by the statement's fetch() function. This mode can be of interest to those users who want to make their client applications more responsive (with more fine-grained operation) by avoiding potentially long blocking times when complete query results are loaded to client's memory.
Note that in the single-row operation:

* bulk queries are not supported, and
* in order to fulfill the expectations of the underlying client library, the complete rowset has to be exhausted before executing further queries on the same session.

Also please note that single rows mode requires PostgreSQL 9 or later, both at
compile- and run-time. If you need to support earlier versions of PostgreSQL,
you can define `SOCI_POSTGRESQL_NOSINLGEROWMODE` when building the library to
disable it.

Once you have created a `session` object as shown above, you can use it to access the database, for example:

```cpp
int count;
sql << "select count(*) from invoices", into(count);
```

(See the [exchanging data](../basics.html">SOCI basics</a> and <a href="../exchange.html) documentation for general information on using the `session` class.)

## SOCI Feature Support

### Dynamic Binding

The PostgreSQL backend supports the use of the SOCI `row` class, which facilitates retrieval of data whose type is not known at compile time.

When calling `row::get<T>()`, the type you should pass as `T` depends upon the underlying database type. For the PostgreSQL backend, this type mapping is:

|PostgreSQL Data Type|SOCI Data Type|`row::get<T>` specializations|
|--- |--- |--- |
|numeric, real, double|dt_double|double|
|boolean, smallint, integer|dt_integer|int|
|int8|dt_long_long|long long|
|oid|dt_integer|unsigned long|
|char, varchar, text, cstring, bpchar|dt_string|std::string|
|abstime, reltime, date, time, timestamp, timestamptz, timetz|dt_date|std::tm|

(See the [dynamic resultset binding](../exchange.html#dynamic) documentation for general information on using the `row` class.)

### Binding by Name

In addition to [binding by position](../exchange.html#bind_position), the PostgreSQL backend supports [binding by name](../exchange.html#bind_name), via an overload of the `use()` function:

```cpp
int id = 7;
sql << "select name from person where id = :id", use(id, "id")
```

Apart from the portable "colon-name" syntax above, which is achieved by rewriting the query string, the backend also supports the PostgreSQL native numbered syntax:

```cpp
int i = 7;
int j = 8;
sql << "insert into t(x, y) values($1, $2)", use(i), use(j);
```

The use of native syntax is not recommended, but can be nevertheless imposed by switching off the query rewriting. This can be achieved by defining the macro `SOCI_POSTGRESQL_NOBINDBYNAME` and it is actually necessary for PostgreSQL 7.3, in which case binding of use elements is not supported at all. See the [Configuration options](#options) section for details.

### Bulk Operations

The PostgreSQL backend has full support for SOCI's [bulk operations](../statements.html#bulk) interface.

### Transactions

[Transactions](../statements.html#transactions) are also fully supported by the PostgreSQL backend.

### blob Data Type

The PostgreSQL backend supports working with data stored in columns of type Blob, via SOCI's [blob](../exchange.html#blob) class with the exception that trimming is not supported.

### rowid Data Type

The concept of row identifier (OID in PostgreSQL) is supported via SOCI's [rowid](../reference.html#rowid) class.

### Nested Statements

Nested statements are not supported by PostgreSQL backend.

### Stored Procedures

PostgreSQL stored procedures can be executed by using SOCI's [procedure](../statements.html#procedures) class.

## Native API Access

SOCI provides access to underlying datbabase APIs via several `get_backend()` functions, as described in the [beyond SOCI](../beyond.html) documentation.

The PostgreSQL backend provides the following concrete classes for navite API access:

|Accessor Function|Concrete Class|
|--- |--- |
|session_backend * session::get_backend()|postgresql_session_backend|
|statement_backend * statement::get_backend()|postgresql_statement_backend|
|blob_backend * blob::get_backend()|postgresql_blob_backend|
|rowid_backend * rowid::get_backend()|postgresql_rowid_backend|

## Backend-specific extensions

### uuid Data Type

The PostgreSQL backend supports working with data stored in columns of type UUID via simple string operations. All string representations of UUID supported by PostgreSQL are accepted on input, the backend will return the standard
format of UUID on output. See the test `test_uuid_column_type_support` for usage examples.

## Configuration options

To support older PostgreSQL versions, the following configuration macros are recognized:

* `SOCI_POSTGRESQL_NOBINDBYNAME` - switches off the query rewriting.
* `SOCI_POSTGRESQL_NOPARAMS` - disables support for parameterized queries (binding of use elements),automatically imposes also the `SOCI_POSTGRESQL_NOBINDBYNAME` macro. It is necessary for PostgreSQL 7.3.
* `SOCI_POSTGRESQL_NOPREPARE` - disables support for separate query preparation, which in this backend is significant only in terms of optimization. It is necessary for PostgreSQL 7.3 and 7.4.
* `SOCI_POSTGRESQL_NOSINLGEROWMODE` - disable single mode retrieving query results row-by-row. It is necessary for PostgreSQL prior to version 9.
