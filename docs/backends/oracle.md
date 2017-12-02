# Oracle Backend Reference

SOCI backend for accessing Oracle database.

## Prerequisites

### Supported Versions

The SOCI Oracle backend is currently supported for use with Oracle 10 or later.
Older versions of Oracle may work as well, but they have not been tested by the SOCI team.

### Tested Platforms

|Oracle|OS|Compiler|
|--- |--- |--- |
|10.2.0 (XE)|RedHat 5|g++ 4.3|
|11.2.0 (XE)|Ubuntu 12.04|g++ 4.6.3|

### Required Client Libraries

The SOCI Oracle backend requires Oracle's `libclntsh` client library. Depending on the particular system, the `libnnz10` library might be needed as well.

Note that the SOCI library itself depends also on `libdl`, so the minimum set of libraries needed to compile a basic client program is:

```console
-lsoci_core -lsoci_oracle -ldl -lclntsh -lnnz10
```

### Connecting to the Database

To establish a connection to an Oracle database, create a `session` object using the oracle backend factory together with a connection string:

```cpp
session sql(oracle, "service=orcl user=scott password=tiger");

// or:
session sql("oracle", "service=orcl user=scott password=tiger");

// or:
session sql("oracle://service=orcl user=scott password=tiger");

// or:
session sql(oracle, "service=//your_host:1521/your_sid  user=scott password=tiger");
```

The set of parameters used in the connection string for Oracle is:

* `service`
* `user`
* `password`
* `mode` (optional; valid values are `sysdba`, `sysoper` and `default`)
* `charset` and `ncharset` (optional; valid values are `utf8`, `utf16`, `we8mswin1252` and `win1252`)

If both `user` and `password` are provided, the session will authenticate using the database credentials, whereas if none of them is set, then external Oracle credentials will be used - this allows integration with so called Oracle wallet authentication.

Once you have created a `session` object as shown above, you can use it to access the database, for example:

```cpp
int count;
sql << "select count(*) from user_tables", into(count);
```

(See the [SOCI basics](../basics.html) and [exchanging data](../exchange.html) documentation for general information on using the `session` class.)#

## SOCI Feature Support

### Dynamic Binding

The Oracle backend supports the use of the SOCI `row` class, which facilitates retrieval of data which type is not known at compile time.

When calling `row::get<T>()`, the type you should pass as `T` depends upon the nderlying database type. For the Oracle backend, this type mapping is:

|Oracle Data Type|SOCI Data Type|`row::get<T>` specializations|
|--- |--- |--- |
|number (where scale > 0)|dt_double|double|
|number(where scale = 0 and precision ≤ `std::numeric_limits<int>::digits10`)|dt_integer|int|
|number|dt_long_long|long long|
|char, varchar, varchar2|dt_string|std::string|
|date|dt_date|std::tm|

(See the [dynamic resultset binding](../exchange.html#dynamic) documentation for general information on using the `row` class.)

### Binding by Name

In addition to [binding by position](../exchange.html#bind_position), the Oracle backend supports [binding by name](../exchange.html#bind_name), via an overload of the `use()` function:

```cpp
int id = 7;
sql << "select name from person where id = :id", use(id, "id")
```

SOCI's use of ':' to indicate a value to be bound within a SQL string is consistant with the underlying Oracle client library syntax.

### Bulk Operations

The Oracle backend has full support for SOCI's [bulk operations](../statements.html#bulk) interface.

### Transactions

[Transactions](../statements.html#transactions) are also fully supported by the Oracle backend,
although transactions with non-default isolation levels have to be managed by explicit SQL statements.

### blob Data Type

The Oracle backend supports working with data stored in columns of type Blob, via SOCI's [blob](../exchange.html#blob) class.

### rowid Data Type

Oracle rowid's are accessible via SOCI's [rowid](../reference.html#rowid) class.

### Nested Statements

The Oracle backend supports selecting into objects of type `statement`, so that you may work with nested sql statements and PL/SQL cursors:

```cpp
statement stInner(sql);
statement stOuter = (sql.prepare <<
    "select cursor(select name from person order by id)"
    " from person where id = 1",
    into(stInner));
stInner.exchange(into(name));
stOuter.execute();
stOuter.fetch();

while (stInner.fetch())
{
    std::cout << name << '\n';
}
```

### Stored Procedures

Oracle stored procedures can be executed by using SOCI's [procedure](../statements.html#procedures) class.

## Native API Access

SOCI provides access to underlying datbabase APIs via several `get_backend()` functions, as described in the [Beyond SOCI](../beyond.html) documentation.

The Oracle backend provides the following concrete classes for navite API access:

|Accessor Function|Concrete Class|
|--- |--- |
|session_backend * session::get_backend()|oracle_session_backend|
|statement_backend * statement::get_backend()|oracle_statement_backend|
|blob_backend * blob::get_backend()|oracle_blob_backend|
|rowid_backend * rowid::get_backend()|oracle_rowid_backend|

## Backend-specific extensions

### oracle_soci_error

The Oracle backend can throw instances of class `oracle_soci_error`, which is publicly derived from `soci_error` and has an additional public `err_num_` member containing the Oracle error code:

```cpp
int main()
{
    try
    {
        // regular code
    }
    catch (oracle_soci_error const &amp; e)
    {
        cerr << "Oracle error: " << e.err_num_
            << " " << e.what() << endl;
    }
    catch (exception const &amp;e)
    {
        cerr << "Some other error: "<< e.what() << endl;
    }
}
```
