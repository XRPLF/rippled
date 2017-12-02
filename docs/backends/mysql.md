# MySQL Backend Reference

SOCI backend for accessing MySQL database.

## Prerequisites

### Supported Versions

The SOCI MySQL backend should in principle work with every version of MySQL 5.x.
Some of the features (transactions, stored functions) are not available when MySQL server doesn't support them.

### Tested Platforms

|MySQL|OS|Compiler|
|--- |--- |--- |
|8.0.1|Windows 10|Visual Studio 2017 (15.3.3)|
|5.5.28|OS X 10.8.2|Apple LLVM version 4.2 (clang-425.0.24)|
|5.0.96|Ubuntu 8.04.4 LTS (Hardy Heron)|g++ (GCC) 4.2.4 (Ubuntu 4.2.4-1ubuntu4)|

### Required Client Libraries

The SOCI MySQL backend requires MySQL's `libmysqlclient` client library from the [MySQL Connector/C](https://dev.mysql.com/downloads/connector/c/).

Note that the SOCI library itself depends also on `libdl`, so the minimum set of libraries needed to compile a basic client program is:

    -lsoci_core -lsoci_mysql -ldl -lmysqlclient

## Connecting to the Database

To establish a connection to a MySQL server, create a `session` object using the `mysql` backend factory together with a connection string:

    session sql(mysql, "db=test user=root password='Ala ma kota'");

    // or:
    session sql("mysql", "db=test user=root password='Ala ma kota'");

    // or:
    session sql("mysql://db=test user=root password='Ala ma kota'");

The set of parameters used in the connection string for MySQL is:

* `dbname` or `db` or `service` (required)
* `user`
* `password` or `pass`
* `host`
* `port`
* `unix_socket`
* `sslca`
* `sslcert`
* `local_infile` - should be `0` or `1`, `1` means `MYSQL_OPT_LOCAL_INFILE` will be set.
* `charset`

Once you have created a `session` object as shown above, you can use it to access the database, for example:

    int count;
    sql << "select count(*) from invoices", into(count);

(See the [SOCI basics]("../basics.html) and [exchanging data](../exchange.html) documentation for general information on using the `session` class.)

## SOCI Feature Support

### Dynamic Binding

The MySQL backend supports the use of the SOCI `row` class, which facilitates retrieval of data which type is not known at compile time.

When calling `row::get&lt;T&gt;()`, the type you should pass as `T` depends upon the underlying database type.
For the MySQL backend, this type mapping is:

|MySQL Data Type|SOCI Data Type|`row::get<T>` specializations|
|--- |--- |--- |
|FLOAT, DOUBLE, DECIMAL and synonyms|dt_double|double|
|TINYINT, TINYINT UNSIGNED, SMALLINT, SMALLINT UNSIGNED, INT|dt_integer|int|
|INT UNSIGNED|dt_long_long|long long or unsigned|
|BIGINT|dt_long_long|long long|
|BIGINT UNSIGNED|dt_unsigned_long_long|unsigned long long|
|CHAR, VARCHAR, BINARY, VARBINARY, TINYBLOB, MEDIUMBLOB, BLOB,LONGBLOB, TINYTEXT, MEDIUMTEXT, TEXT, LONGTEXT, ENUM|dt_string|std::string|
|TIMESTAMP (works only with MySQL >= 5.0), DATE, TIME, DATETIME|dt_date|std::tm|

(See the [dynamic resultset binding](../exchange.html#dynamic) documentation for general information on using the `Row` class.)

### Binding by Name

In addition to [binding by position](../exchange.html#bind_position), the MySQL backend supports
[binding by name](../exchange.html#bind_name), via an overload of the `use()` function:

    int id = 7;
    sql << "select name from person where id = :id", use(id, "id")

It should be noted that parameter binding of any kind is supported only by means of emulation, since the underlying API used by the backend doesn't provide this feature.

### Bulk Operations

[Transactions](../statements.html#transactions) are also supported by the MySQL backend. Please note, however, that transactions can only be used when the MySQL server supports them (it depends on options used during the compilation of the server; typically, but not always, servers >=4.0 support transactions and earlier versions do not) and only with appropriate table types.

### BLOB Data Type

SOCI `blob` interface is not supported by the MySQL backend.

Note that this does not mean you cannot use MySQL's `BLOB` types.  They can be selected using the usual SQL syntax and read into `std::string` on the C++ side, so no special interface is required.

### RowID Data Type

The `rowid` functionality is not supported by the MySQL backend.

### Nested Statements

Nested statements are not supported by the MySQL backend.

### Stored Procedures

MySQL version 5.0 and later supports two kinds of stored routines: stored procedures and stored functions (for details, please consult the [procedure MySQL documentation](http://dev.mysql.com/doc/refman/5.0/en/stored-procedures.html)). Stored functions can be executed by using SOCI's [procedure class](../statements.html#procedures). There is currently no support for stored procedures.

## Native API Access

SOCI provides access to underlying datbabase APIs via several `get_backend()` functions, as described in the [Beyond SOCI](../beyond.html) documentation.

The MySQL backend provides the following concrete classes for native API access:

|Accessor Function|Concrete Class|
|--- |--- |
|session_backend * session::get_backend()|mysql_session_backend|
|statement_backend * statement::get_backend()|mysql_statement_backend|

## Backend-specific extensions

None.

## Configuration options

None.
