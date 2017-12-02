# ODBC Backend Reference

SOCI backend for accessing variety of databases via ODBC API.

## Prerequisites

### Supported Versions

The SOCI ODBC backend is supported for use with ODBC 3.

### Tested Platforms

|ODBC|OS|Compiler|
|--- |--- |--- |
|3|Linux (Ubuntu 12.04)|g++ 4.6.3|
|3|Linux (Ubuntu 12.04)|clang 3.2|
|3.8|Windows 8|Visual Studio 2012|
|3|Windows 7|Visual Studio 2010|
|3|Windows XP|Visual Studio 2005 (express)|
|3|Windows XP|Visual C++ 8.0 Professional|
|3|Windows XP|g++ 3.3.4 (Cygwin)|

### Required Client Libraries

The SOCI ODBC backend requires the ODBC client library.

## Connecting to the Database

To establish a connection to the ODBC database, create a Session object using the `ODBC` backend factory together with a connection string:

```cpp
backend_factory const&amp; backEnd = odbc;
session sql(backEnd, "filedsn=c:\\my.dsn");
```

or simply:

```cpp
session sql(odbc, "filedsn=c:\\my.dsn");
```

The set of parameters used in the connection string for ODBC is the same as accepted by the [SQLDriverConnect](http://msdn.microsoft.com/library/default.asp?url=/library/en-us/odbcsql/od_odbc_d_4x4k.asp) function from the ODBC library.

Once you have created a `session` object as shown above, you can use it to access the database, for example:

```cpp
int count;
sql << "select count(*) from invoices", into(count);
```

(See the [SOCI basics](../basics.html) and [exchanging data](../exchange.html) documentation for general information on using the `session` class.)

## SOCI Feature Support

### Dynamic Binding

The ODBC backend supports the use of the SOCI `row` class, which facilitates retrieval of data whose type is not known at compile time.

When calling `row::get<T>()`, the type you should pass as T depends upon the underlying database type.
For the ODBC backend, this type mapping is:

|ODBC Data Type|SOCI Data Type|`row::get<T>` specializations|
|--- |--- |--- |
|SQL_DOUBLE, SQL_DECIMAL, SQL_REAL, SQL_FLOAT, SQL_NUMERIC|dt_double|double|
|SQL_TINYINT, SQL_SMALLINT, SQL_INTEGER, SQL_BIGINT|dt_integer|int|
|SQL_CHAR, SQL_VARCHAR|dt_string|std::string|
|SQL_TYPE_DATE, SQL_TYPE_TIME, SQL_TYPE_TIMESTAMP|dt_date|std::tm|

Not all ODBC drivers support all datatypes.

(See the [dynamic resultset binding](../exchange.html#dynamic) documentation for general information on using the `row` class.)

### Binding by Name

In addition to [binding by position](../exchange.html#bind_position), the ODBC backend supports [binding by name](../exchange.html#bind_name), via an overload of the `use()` function:

```cpp
int id = 7;
sql << "select name from person where id = :id", use(id, "id")
```

Apart from the portable "colon-name" syntax above, which is achieved by rewriting the query string, the backend also supports the ODBC ? syntax:

```cpp
int i = 7;
int j = 8;
sql << "insert into t(x, y) values(?, ?)", use(i), use(j);
```

### Bulk Operations

The ODBC backend has support for SOCI's [bulk operations](../statements.html#bulk) interface.  Not all ODBC drivers support bulk operations, the following is a list of some tested backends:

|ODBC Driver|Bulk Read|Bulk Insert|
|--- |--- |--- |
|MS SQL Server 2005|YES|YES|
|MS Access 2003|YES|NO|
|PostgresQL 8.1|YES|YES|
|MySQL 4.1|NO|NO|

### Transactions

[Transactions](../statements.html#transactions) are also fully supported by the ODBC backend, provided that they are supported by the underlying database.

### BLOB Data Type

Not currently supported.

### RowID Data Type

Not currently supported.

### Nested Statements

Not currently supported.

### Stored Procedures

Not currently supported.

## Native API Access

SOCI provides access to underlying datbabase APIs via several getBackEnd() functions, as described in the [beyond SOCI](../beyond.html) documentation.

The ODBC backend provides the following concrete classes for navite API access:

|Accessor Function|Concrete Class|
|--- |--- |
|session_backend* session::get_backend()|odbc_statement_backend|
|statement_backend* statement::get_backend()|odbc_statement_backend|
|rowid_backend* rowid::get_backend()|odbc_rowid_backend|

## Backend-specific extensions

### odbc_soci_error

The ODBC backend can throw instances of class `odbc_soci_error`, which is publicly derived from `soci_error` and has additional public members containing the ODBC error code, the Native database error code, and the message returned from ODBC:

```cpp
int main()
{
    try
    {
        // regular code
    }
    catch (soci::odbc_soci_error const&amp; e)
    {
        cerr << "ODBC Error Code: " << e.odbc_error_code() << endl
                << "Native Error Code: " << e.native_error_code() << endl
                << "SOCI Message: " << e.what() << std::endl
                << "ODBC Message: " << e.odbc_error_message() << endl;
    }
    catch (exception const &amp;e)
    {
        cerr << "Some other error: " << e.what() << endl;
    }
}
```

### get_connection_string()

The `odbc_session_backend` class provides `std::string get_connection_string() const` method
that returns fully expanded connection string as returned by the `SQLDriverConnect` function.

## Configuration options

This backend supports `odbc_option_driver_complete` option which can be passed to it via `connection_parameters` class. The value of this option is passed to `SQLDriverConnect()` function as "driver completion" parameter and so must be one of `SQL_DRIVER_XXX` values, in the string form. The default value of this option is `SQL_DRIVER_PROMPT` meaning that the driver will query the user for the user name and/or the password if they are not stored together with the connection. If this is undesirable for some reason, you can use `SQL_DRIVER_NOPROMPT` value for this option to suppress showing the message box:

```cpp
connection_parameters parameters("odbc", "DSN=mydb");
parameters.set_option(odbc_option_driver_complete, "0" /* SQL_DRIVER_NOPROMPT */);
session sql(parameters);
```
