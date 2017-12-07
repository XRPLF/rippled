# DB2 Backend Reference

SOCI backend for accessing IBM DB2 database.

## Prerequisites

### Supported Versions

See [Tested Platforms](#tested-platforms).

### Tested Platforms

|DB2 |OS|Compiler|
|--- |--- |--- |
|-|Linux PPC64|GCC|
|9.1|Linux|GCC|
|9.5|Linux|GCC|
|9.7|Linux|GCC|
|10.1|Linux|GCC|
|10.1|Windows 8|Visual Studio 2012|

### Required Client Libraries

The SOCI DB2 backend requires client library from the [IBM Data Server Driver Package (DS Driver)](https://www-01.ibm.com/support/docview.wss?uid=swg21385217).

## Connecting to the Database

On Unix, before using the DB2 backend please make sure, that you have sourced DB2 profile into your environment:

```console
. ~/db2inst1/sqllib/db2profile
```

To establish a connection to the DB2 database, create a session object using the DB2
backend factory together with the database file name:

```cpp
soci::session sql(soci::db2, "your DB2 connection string here");
```

## SOCI Feature Support

### Dynamic Binding

TODO

### Bulk Operations

Supported, but with caution as it hasn't been extensively tested.

### Transactions

Currently, not supported.

### BLOB Data Type

Currently, not supported.

### Nested Statements

Nesting statements are not processed by SOCI in any special way and they work as implemented
by the DB2 database.

### Stored Procedures

Stored procedures are supported, with `CALL` statement.

## Native API Access

TODO

## Backend-specific extensions

None.

## Configuration options

This backend supports `db2_option_driver_complete` option which can be passed to it via
`connection_parameters` class. The value of this option is passed to `SQLDriverConnect()`
function as "driver completion" parameter and so must be one of `SQL_DRIVER_XXX` values,
in the string form. The default value of this option is `SQL_DRIVER_PROMPT` meaning
that the driver will query the user for the user name and/or the password if they are
not stored together with the connection. If this is undesirable for some reason,
you can use `SQL_DRIVER_NOPROMPT` value for this option to suppress showing the message box:

```cpp
connection_parameters parameters("db2", "DSN=sample");
parameters.set_option(db2_option_driver_complete, "0" /* SQL_DRIVER_NOPROMPT */);
session sql(parameters);
```

Note, `db2_option_driver_complete` controls driver completion specific to the IBM DB2 driver
for ODBC and CLI.
