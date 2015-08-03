## PostgreSQL Backend Reference

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
      * [UUID Data Type](#uuid)
* [Configuration options](#configuration)

### <a name="prerequisites"></a> Prerequisites

#### <a name="versions"></a> Supported Versions

The SOCI PostgreSQL backend is supported for use with PostgreSQL >= 7.3, although versions older than 8.0 will suffer from limited feature support. See below for details.

#### <a name="tested"></a> Tested Platforms

<table>
<tbody>
<tr><th>PostgreSQL version</th><th>Operating System</th><th>Compiler</th></tr>
<tr><td>9.0</td><td>Mac OS X 10.6.6</td><td>g++ 4.2</td></tr>
<tr><td>8.4</td><td>FreeBSD 8.2</td><td>g++ 4.1</td></tr>
<tr><td>8.4</td><td>Debian 6</td><td>g++ 4.3</td></tr>
<tr><td>8.4</td><td>RedHat 5</td><td>g++ 4.3</td></tr>
</tbody>
</table>

#### <a name="required"></a> Required Client Libraries

The SOCI PostgreSQL backend requires PostgreSQL's `libpq` client library.

Note that the SOCI library itself depends also on `libdl`, so the minimum set of libraries needed to compile a basic client program is:

    -lsoci_core -lsoci_postgresql -ldl -lpq

#### <a name="connecting"></a> Connecting to the Database

To establish a connection to the PostgreSQL database, create a `session` object using the `postgresql` backend factory together with a connection string:


    session sql(postgresql, "dbname=mydatabase");

    // or:
    session sql("postgresql", "dbname=mydatabase");

    // or:
    session sql("postgresql://dbname=mydatabase");

The set of parameters used in the connection string for PostgreSQL is the same as accepted by the `[PQconnectdb](http://www.postgresql.org/docs/8.3/interactive/libpq.html#LIBPQ-CONNECT)` function from the `libpq` library.

Once you have created a `session` object as shown above, you can use it to access the database, for example:

    int count;
    sql << "select count(*) from invoices", into(count);

(See the [exchanging data](../basics.html">SOCI basics</a> and <a href="../exchange.html) documentation for general information on using the `session` class.)

### <a name="features"></a>  SOCI Feature Support

#### <a name="dynamic"></a> Dynamic Binding

The PostgreSQL backend supports the use of the SOCI `row` class, which facilitates retrieval of data whose type is not known at compile time.

When calling `row::get<T>()`, the type you should pass as `T` depends upon the underlying database type.<br/> For the PostgreSQL backend, this type mapping is:

<table>
  <tbody>
    <tr>
      <th>PostgreSQL Data Type</th>
      <th>SOCI Data Type</th>
      <th><code>row::get&lt;T&gt;</code> specializations</th>
    </tr>
    <tr>
      <td>numeric, real, double</td>
      <td><code>dt_double</code></td>
      <td><code>double</code></td>
    </tr>
    <tr>
      <td>boolean, smallint, integer</td>
      <td><code>dt_integer</code></td>
      <td><code>int</code></td>
    </tr>
    <tr>
      <td>int8</td>
      <td><code>dt_long_long</code></td>
      <td><code>long long</code></td>
    </tr>
    <tr>
      <td>oid</td>
      <td><code>dt_integer</code></td>
      <td><code>unsigned long</code></td>
    </tr>
    <tr>
      <td>char, varchar, text, cstring, bpchar</td>
      <td><code>dt_string</code></td>
      <td><code>std::string</code></td>
    </tr>
    <tr>
      <td>abstime, reltime, date, time, timestamp, timestamptz, timetz</td>
      <td><code>dt_date</code></td>
      <td><code>std::tm</code></code></code></td>
    </tr>
  </tbody>
</table>

(See the [dynamic resultset binding](../exchange.html#dynamic) documentation for general information on using the `row` class.)

#### <a name="name"></a> Binding by Name

In addition to [binding by position](../exchange.html#bind_position), the PostgreSQL backend supports [binding by name](../exchange.html#bind_name), via an overload of the `use()` function:

    int id = 7;
    sql << "select name from person where id = :id", use(id, "id")

Apart from the portable "colon-name" syntax above, which is achieved by rewriting the query string, the backend also supports the PostgreSQL native numbered syntax:

    int i = 7;
    int j = 8;
    sql << "insert into t(x, y) values($1, $2)", use(i), use(j);

The use of native syntax is not recommended, but can be nevertheless imposed by switching off the query rewriting. This can be achieved by defining the macro `SOCI_POSTGRESQL_NOBINDBYNAME` and it is actually necessary for PostgreSQL 7.3, in which case binding of use elements is not supported at all. See the [Configuration options](#options) section for details.

#### <a name="bulk"></a> Bulk Operations

The PostgreSQL backend has full support for SOCI's [bulk operations](../statements.html#bulk) interface.

#### <a name="transactions"></a> Transactions

[Transactions](../statements.html#transactions) are also fully supported by the PostgreSQL backend.

#### <a name="blob"></a> blob Data Type

The PostgreSQL backend supports working with data stored in columns of type Blob, via SOCI's [blob](../exchange.html#blob) class with the exception that trimming is not supported.

#### <a name="rowid"></a> rowid Data Type

The concept of row identifier (OID in PostgreSQL) is supported via SOCI's [rowid](../reference.html#rowid) class.

#### <a name="nested"></a> Nested Statements

Nested statements are not supported by PostgreSQL backend.

#### <a name="stored"></a> Stored Procedures

PostgreSQL stored procedures can be executed by using SOCI's [procedure](../statements.html#procedures) class.

### <a name="native"></a> Acessing the native database API

SOCI provides access to underlying datbabase APIs via several `get_backend()` functions, as described in the [beyond SOCI](../beyond.html) documentation.

The PostgreSQL backend provides the following concrete classes for navite API access:

<table>
  <tbody>
    <tr>
      <th>Accessor Function</th>
      <th>Concrete Class</th>
    </tr>
    <tr>
      <td><code>session_backend * session::get_backend()</code></td>
      <td><code>postgresql_session_backend</code></td>
    </tr>
    <tr>
      <td><code>statement_backend * statement::get_backend()</code></td>
      <td><code>postgresql_statement_backend</code></td>
    </tr>
    <tr>
      <td><code>blob_backend * blob::get_backend()</code></td>
      <td><code>postgresql_blob_backend</code></td>
    </tr>
    <tr>
      <td><code>rowid_backend * rowid::get_backend()</code></td>
      <td><code>postgresql_rowid_backend</code></td>
    </tr>
  </tbody>
</table>


### <a name="extensions"></a> Backend-specific extensions

#### <a name="uuid"></a> uuid Data Type

The PostgreSQL backend supports working with data stored in columns of type UUID via simple string operations. All string representations of UUID supported by PostgreSQL are accepted on input, the backend will return the standard
format of UUID on output. See the test `test_uuid_column_type_support` for usage examples.

### <a name="configuration"></a> Configuration options

To support older PostgreSQL versions, the following configuration macros are recognized:

* `SOCI_POSTGRESQL_NOBINDBYNAME` - switches off the query rewriting.
* `SOCI_POSTGRESQL_NOPARAMS` - disables support for parameterized queries (binding of use elements),automatically imposes also the `SOCI_POSTGRESQL_NOBINDBYNAME` macro. It is necessary for PostgreSQL 7.3.
* `SOCI_POSTGRESQL_NOPREPARE` - disables support for separate query preparation, which in this backend is significant only in terms of optimization. It is necessary for PostgreSQL 7.3 and 7.4.