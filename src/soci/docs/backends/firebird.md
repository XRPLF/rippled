# Firebird Backend Reference

SOCI backend for accessing Firebird database.

## Prerequisites

### Supported Versions

The SOCI Firebird backend supports versions of Firebird from 1.5 to 2.5 and can be used with
either the client-server or embedded Firebird libraries.
The former is the default, to select the latter set `SOCI_FIREBIRD_EMBEDDED` CMake option to `ON`
value when building.

### Tested Platforms

|Firebird |OS|Compiler|
|--- |--- |--- |
|1.5.2.4731|SunOS 5.10|g++ 3.4.3|
|1.5.2.4731|Windows XP|Visual C++ 8.0|
|1.5.3.4870|Windows XP|Visual C++ 8.0 Professional|
|2.5.2.26540|Debian GNU/Linux 7|g++ 4.7.2|

### Required Client Libraries

The Firebird backend requires Firebird's `libfbclient` client library.
For example, on Ubuntu Linux, for example, `firebird-dev` package and its dependencies are required.

## Connecting to the Database

To establish a connection to a Firebird database, create a Session object using the firebird
backend factory together with a connection string:

```cpp
BackEndFactory const &backEnd = firebird;
session sql(backEnd, "service=/usr/local/firbird/db/test.fdb user=SYSDBA password=masterkey");
```

or simply:

```cpp
session sql(firebird, "service=/usr/local/firbird/db/test.fdb user=SYSDBA password=masterkey");
```

The set of parameters used in the connection string for Firebird is:

* service
* user
* password
* role
* charset

The following parameters have to be provided as part of the connection string : *service*, *user*,
*password*. Role and charset parameters are optional.

Once you have created a `session` object as shown above, you can use it to access the database, for example:

```cpp
int count;
sql << "select count(*) from user_tables", into(count);
```

(See the [SOCI basics](../basics.html) and [exchanging data](../exchange.html) documentation
for general information on using the `session` class.)

## SOCI Feature Support

### Dynamic Binding

The Firebird backend supports the use of the SOCI `row` class, which facilitates retrieval of data whose
type is not known at compile time.

When calling `row::get<T>()`, the type you should pass as T depends upon the underlying database type.
For the Firebird backend, this type mapping is:

|Firebird Data Type|SOCI Data Type|`row::get<T>` specializations|
|--- |--- |--- |
|numeric, decimal (where scale > 0)|eDouble|double|
|numeric, decimal [^1] (where scale = 0)|eInteger, eDouble|int, double|
|double precision, float|eDouble|double|
|smallint, integer|eInteger|int|
|char, varchar|eString|std::string|
|date, time, timestamp|eDate|std::tm|

[^1] There is also 64bit integer type for larger values which is
currently not supported.

(See the [dynamic resultset binding](../exchange.html#dynamic) documentation for general information
on using the `Row` class.)

### Binding by Name

In addition to [binding by position](../exchange.html#bind_position), the Firebird backend supports
[binding by name](../exchange.html#bind_name), via an overload of the `use()` function:

    int id = 7;
    sql << "select name from person where id = :id", use(id, "id")

It should be noted that parameter binding by name is supported only by means of emulation,
since the underlying API used by the backend doesn't provide this feature.

### Bulk Operations

The Firebird backend has full support for SOCI [bulk operations](../statements.html#bulk) interface.
This feature is also supported by emulation.

### Transactions

[Transactions](../statements.html#transactions) are also fully supported by the Firebird backend.
In fact, an implicit transaction is always started when using this backend if one hadn't been
started by explicitly calling `begin()` before. The current transaction is automatically
committed in `session` destructor.

### BLOB Data Type

The Firebird backend supports working with data stored in columns of type Blob,
via SOCI `[BLOB](../exchange.html#blob)` class.

It should by noted, that entire Blob data is fetched from database to allow random read and write access.
This is because Firebird itself allows only writing to a new Blob or reading from existing one -
modifications of existing Blob means creating a new one.
Firebird backend hides those details from user.

### RowID Data Type

This feature is not supported by Firebird backend.

### Nested Statements

This feature is not supported by Firebird backend.

### Stored Procedures

Firebird stored procedures can be executed by using SOCI [Procedure](../statements.html#procedures) class.

## Native API Access

SOCI provides access to underlying datbabase APIs via several getBackEnd() functions,
as described in the [beyond SOCI](../beyond.html) documentation.

The Firebird backend provides the following concrete classes for navite API access:

|Accessor Function|Concrete Class|
|--- |--- |
|SessionBackEnd* Session::getBackEnd()|FirebirdSessionBackEnd|
|StatementBackEnd* Statement::getBackEnd()|FirebirdStatementBackEnd|
|BLOBBackEnd* BLOB::getBackEnd()|FirebirdBLOBBackEnd|
|RowIDBackEnd* RowID::getBackEnd()|

## Backend-specific extensions

### FirebirdSOCIError

The Firebird backend can throw instances of class `FirebirdSOCIError`, which is publicly derived
from `SOCIError` and has an additional public `status_` member containing the Firebird status vector.
