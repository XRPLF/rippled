## SQLite3 Backend Reference

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
      * [SQLite3 result code support](#sqlite3result)
* [Configuration options](#options)


### <a name="prerequisites"></a> Prerequisites

#### <a name="versions"></a> Supported Versions

The SOCI SQLite3 backend is supported for use with SQLite3 >= 3.1

#### <a name="tested"></a> Tested Platforms

<table>
<tbody>
<tr><th>SQLite3 version</th><th>Operating System</th><th>Compiler</th></tr>
<tr><td>3.5.2</td><td>Mac OS X 10.5</td><td>g++ 4.0.1</td></tr>
<tr><td>3.1.3</td><td>Mac OS X 10.4</td><td>g++ 4.0.1</td></tr>
<tr><td>3.2.1</td><td>Linux i686 2.6.10-gentoo-r6</td><td>g++ 3.4.5</td></tr>
<tr><td>3.3.4</td><td>Ubuntu 5.1</td><td>g++ 4.0.2</td></tr>
<tr><td>3.3.4</td><td>Windows XP</td><td>(cygwin) g++ 3.3.4</td></tr>
<tr><td>3.3.4</td><td>Windows XP</td><td>Visual C++ 2005 Express Edition</td></tr>
<tr><td>3.3.8</td><td>Windows XP</td><td>Visual C++ 2005 Professional</td></tr>
<tr><td>3.4.0</td><td>Windows XP</td><td>(cygwin) g++ 3.4.4</td></tr>
<tr><td>3.4.0</td><td>Windows XP</td><td>Visual C++ 2005 Express Edition</td></tr>
</tbody>
</table>

#### <a name="required"></a> Required Client Libraries

The SOCI SQLite3 backend requires SQLite3's `libsqlite3` client library.

#### <a name="connecting"></a> Connecting to the Database

To establish a connection to the SQLite3 database, create a Session object using the `SQLite3` backend factory together with the database file name:

    session sql(sqlite3, "database_filename");

    // or:

    session sql("sqlite3", "db=db.sqlite timeout=2 shared_cache=true");

The set of parameters used in the connection string for SQLite is:

* `dbname` or `db`
* `timeout` - set sqlite busy timeout (in seconds) ([link](http://www.sqlite.org/c3ref/busy_timeout.html)
* `synchronous` - set the pragma synchronous flag ([link](http://www.sqlite.org/pragma.html#pragma_synchronous))
* `shared_cache` - should be `true` ([link](http://www.sqlite.org/c3ref/enable_shared_cache.html))

Once you have created a `session` object as shown above, you can use it to access the database, for example:

    int count;
    sql << "select count(*) from invoices", into(count);

(See the [SOCI basics](../basics.html) and [exchanging data](../exchange.html) documentation for general information on using the `session` class.)

### <a name="features"></a> SOCI Feature Support

#### <a name="dynamic"></a> Dynamic Binding

The SQLite3 backend supports the use of the SOCI `row` class, which facilitates retrieval of data whose type is not known at compile time.

When calling `row::get<T>()`, the type you should pass as T depends upon the underlying database type.

For the SQLite3 backend, this type mapping is complicated by the fact the SQLite3 does not enforce [types][INTEGER_PRIMARY_KEY] and makes no attempt to validate the type names used in table creation or alteration statements. SQLite3 will return the type as a string, SOCI will recognize the following strings and match them the corresponding SOCI types:

<table>
  <tbody>
    <tr>
      <th>SQLite3 Data Type</th>
      <th>SOCI Data Type</th>
      <th><code>row::get&lt;T&gt;</code> specializations</th>
    </tr>
    <tr>
      <td>*float*, *double*</td>
      <td><code>dt_double</code></td>
      <td><code>double</code></td>
    </tr>
    <tr>
      <td>*int8*, *bigint*</td>
      <td><code>dt_long_long</code></td>
      <td><code>long long</code></td>
    </tr>
    <tr>
      <td>*unsigned big int*</td>
      <td><code>dt_unsigned_long_long</code></td>
      <td><code>unsigned long long</code></td>
    </tr>
    <tr>
      <td>*int*, *boolean*</td>
      <td><code>dt_integer</code></td>
      <td><code>int</code></td>
    </tr>
    <tr>
      <td>*text, *char*</td>
      <td><code>dt_string</code></td>
      <td><code>std::string</code></td>
    </tr>
    <tr>
      <td>*date*, *time*</td>
      <td><code>dt_date</code></td>
      <td><code>std::tm</code></code></code></td>
    </tr>
  </tbody>
</table>

[INTEGER_PRIMARY_KEY] : There is one case where SQLite3 enforces type. If a column is declared as "integer primary key", then SQLite3 uses that as an alias to the internal ROWID column that exists for every table.  Only integers are allowed in this column.

(See the [dynamic resultset binding](../exchange.html#dynamic) documentation for general information on using the `row` class.)

#### <a name="name"></a> Binding by Name

In addition to [binding by position](../exchange.html#bind_position), the SQLite3 backend supports [binding by name](../exchange.html#bind_name), via an overload of the `use()` function:

    int id = 7;
    sql << "select name from person where id = :id", use(id, "id")

The backend also supports the SQLite3 native numbered syntax, "one or more literals can be replace by a parameter "?" or ":AAA" or "@AAA" or "$VVV" where AAA is an alphanumeric identifier and VVV is a variable name according to the syntax rules of the TCL programming language." [[1]](http://www.sqlite.org/capi3ref.html#sqlite3_bind_int):

    int i = 7;
    int j = 8;
    sql << "insert into t(x, y) values(?, ?)", use(i), use(j);

#### <a name="bulk"></a> Bulk Operations

The SQLite3 backend has full support for SOCI's [bulk operations](../statements.html#bulk) interface.  However, this support is emulated and is not native.

#### <a name="transactions"></a> Transactions

[Transactions](../statements.html#transactions) are also fully supported by the SQLite3 backend.

#### <a name="blob"></a> BLOB Data Type

The SQLite3 backend supports working with data stored in columns of type Blob, via SOCI's blob class. Because of SQLite3 general typelessness the column does not have to be declared any particular type.

#### <a name="rowid"></a> RowID Data Type

In SQLite3 RowID is an integer.  "Each entry in an SQLite table has a unique integer key called the "rowid". The rowid is always available as an undeclared column named ROWID, OID, or _ROWID_. If the table has a column of type INTEGER PRIMARY KEY then that column is another an alias for the rowid."[[2]](http://www.sqlite.org/capi3ref.html#sqlite3_last_insert_rowid)

#### <a name="nested"></a> Nested Statements

Nested statements are not supported by SQLite3 backend.

#### <a name="stored"></a> Stored Procedures

Stored procedures are not supported by SQLite3 backend

### <a name="native"></a> Acessing the native database API

SOCI provides access to underlying datbabase APIs via several `get_backend()` functions, as described in the [beyond SOCI](../beyond.html) documentation.

The SQLite3 backend provides the following concrete classes for navite API access:

<table>
  <tbody>
    <tr>
      <th>Accessor Function</th>
      <th>Concrete Class</th>
    </tr>
    <tr>
      <td><code>session_backend* session::get_backend()</code></td>
      <td><code>sqlie3_session_backend</code></td>
    </tr>
    <tr>
      <td><code>statement_backend* statement::get_backend()</code></td>
      <td><code>sqlite3_statement_backend</code></td>
    </tr>
    <tr>
      <td><code>rowid_backend* rowid::get_backend()</code></td>
      <td><code>sqlite3_rowid_backend</code></td>
    </tr>
  </tbody>
</table>

### <a name="extensions"></a> Backend-specific extensions

#### <a name="sqlite3result"></a> SQLite3 result code support

SQLite3 result code is provided via the backend specific `sqlite3_soci_error` class. Catching the backend specific error yields the value of SQLite3 result code via the `result()` method.

### <a name="configuration"></a> Configuration options

None