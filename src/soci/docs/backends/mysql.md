## MySQL Backend Reference

* [Prerequisites](#prerequisites)
    * [Supported Versions](#versions)
    * [Tested Platforms](#tested)
    * [Required Client Libraries](#required)
* [Connecting to the Database](#connecting)
* [SOCI Feature Support](#support)
    * [Dynamic Binding](#dynamic)
    * [Binding by Name](#name)
    * [Bulk Operations](#bulk)
    * [Transactions](#transactions)
    * [BLOB Data Type](#blob)
    * [RowID Data Type](#rowid)
    * [Nested Statements](#nested)
    * [Stored Procedures](#stored)
* [Accessing the Native Database API](#native)
* [Backend-specific Extensions](#extensions)
* [Configuration options](#configuration)



### <a name="prerequisites"></a> Prerequisites

#### <a name="versions"></a> Supported Versions

The SOCI MySQL backend should in principle work with every version of MySQL 5.x. Some of the features (transactions, stored functions) are not available when MySQL server doesn't support them.

#### <a name="tested"></a> Tested Platforms

<table>
<tbody>
<tr><th>MySQL version</th><th>Operating System</th><th>Compiler</th></tr>
<tr><td>5.5.28</td><td>OS X 10.8.2</td><td>Apple LLVM version 4.2
(clang-425.0.24)</td></tr>
<tr><td>5.0.96</td><td>Ubuntu 8.04.4 LTS (Hardy Heron)</td>
<td>g++ (GCC) 4.2.4 (Ubuntu 4.2.4-1ubuntu4)</td>
</tr>
</tbody>
</table>

#### <a name="required"></a> Required Client Libraries

The SOCI MySQL backend requires MySQL's `libmysqlclient` client library.

Note that the SOCI library itself depends also on `libdl`, so the minimum set of libraries needed to compile a basic client program is:

    -lsoci_core -lsoci_mysql -ldl -lmysqlclient

### <a name="connecting"></a> Connecting to the Database

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


### <a name="support"></a> SOCI Feature Support

#### <a name="dynamic"></a> Dynamic Binding

The MySQL backend supports the use of the SOCI `row` class, which facilitates retrieval of data which type is not known at compile time.

When calling `row::get&lt;T&gt;()`, the type you should pass as `T` depends upon the underlying database type.
For the MySQL backend, this type mapping is:

<table>
  <tbody>
    <tr>
      <th>MySQL Data Type</th>
      <th>SOCI Data Type</th>
      <th><code>row::get&lt;T&gt;</code> specializations</th>
    </tr>
    <tr>
      <td>FLOAT, DOUBLE, DECIMAL and synonyms</td>
      <td><code>dt_double</code></td>
      <td><code>double</code></td>
    </tr>
    <tr>
      <td>TINYINT, TINYINT UNSIGNED, SMALLINT, SMALLINT UNSIGNED, INT</td>
      <td><code>dt_integer</code></td>
      <td><code>int</code></td>
    </tr>
    <tr>
      <td>INT UNSIGNED</td>
      <td><code>dt_long_long</code></td>
      <td><code>long long</code> or </code>unsigned</code></td>
    </tr>
    <tr>
      <td>BIGINT</td>
      <td><code>dt_long_long</code></td>
      <td><code>long long</code></td>
    </tr>
    <tr>
      <td>BIGINT UNSIGNED</td>
      <td><code>dt_unsigned_long_long</code></td>
      <td><code>unsigned long long</code></td>
    </tr>
    <tr>
      <td>CHAR, VARCHAR, BINARY, VARBINARY, TINYBLOB, MEDIUMBLOB, BLOB,
      LONGBLOB, TINYTEXT, MEDIUMTEXT, TEXT, LONGTEXT, ENUM</td>
      <td><code>dt_string</code></td>
      <td><code>std::string</code></td>
    </tr>
    <tr>
      <td>TIMESTAMP (works only with MySQL >=&nbsp;5.0), DATE,
      TIME, DATETIME</td>
      <td><code>dt_date</code></td>
      <td><code>std::tm</code></td>
    </tr>
  </tbody>
</table>

(See the [dynamic resultset binding](../exchange.html#dynamic) documentation for general information on using the `Row` class.)

#### <a name="name"></a> Binding by Name

In addition to [binding by position](../exchange.html#bind_position), the MySQL backend supports
[binding by name](../exchange.html#bind_name), via an overload of the `use()` function:

    int id = 7;
    sql << "select name from person where id = :id", use(id, "id")

It should be noted that parameter binding of any kind is supported only by means of emulation, since the underlying API used by the backend doesn't provide this feature.

#### <a name="bulk"></a> Bulk Operations

[Transactions](../statements.html#transactions) are also supported by the MySQL backend. Please note, however, that transactions can only be used when the MySQL server supports them (it depends on options used during the compilation of the server; typically, but not always, servers >=4.0 support transactions and earlier versions do not) and only with appropriate table types.

#### <a name="blob"></a> BLOB Data Type

SOCI `blob` interface is not supported by the MySQL backend.

Note that this does not mean you cannot use MySQL's `BLOB` types.  They can be selected using the usual SQL syntax and read into `std::string` on the C++ side, so no special interface is required.

#### <a name="rowid"></a> RowID Data Type

The `rowid` functionality is not supported by the MySQL backend.

#### <a name="nested"></a> Nested Statements

Nested statements are not supported by the MySQL backend.

#### <a name="stored"></a> Stored Procedures

MySQL version 5.0 and later supports two kinds of stored routines: stored procedures and stored functions (for details, please consult the [procedure MySQL documentation](http://dev.mysql.com/doc/refman/5.0/en/stored-procedures.html)). Stored functions can be executed by using SOCI's [procedure class](../statements.html#procedures). There is currently no support for stored procedures.


### <a name="native"></a> Accessing the native database API

SOCI provides access to underlying datbabase APIs via several `get_backend()` functions, as described in the [Beyond SOCI](../beyond.html) documentation.

The MySQL backend provides the following concrete classes for native API access:

<table>
  <tbody>
    <tr>
      <th>Accessor Function</th>
      <th>Concrete Class</th>
    </tr>
    <tr>
      <td><code>session_backend * session::get_backend()</code></td>
      <td><code>mysql_session_backend</code></td>
    </tr>
    <tr>
      <td><code>statement_backend * statement::get_backend()</code></td>
      <td><code>mysql_statement_backend</code></td>
    </tr>
  </tbody>
</table>

### <a name="extensions"></a> Backend-specific extensions

None.

### <a name="configuration"></a> Configuration options

None.