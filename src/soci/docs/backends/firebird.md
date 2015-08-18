## Firebird Backend Reference

* [Prerequisites](#prerequisites)
    * [Supported Versions](#versions)
    * [Tested Platforms](#platforms)
    * [Required Client Libraries](#required)
* [Connecting to the Database](#connecting)
* [SOCI Feature Support](#features)
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
    * [FirebirdSOCIError](#firebirdsocierror)

### <a name="prerequisites"></a> Prerequisites

#### <a name="versions"></a> Supported Versions

The SOCI Firebird backend supports versions of Firebird from 1.5 to 2.5 and can be used with either the client-server or embedded Firebird libraries. The former is the default, to select the latter set <tt>SOCI_FIREBIRD_EMBEDDED</tt> CMake option to <tt>ON</tt> value when building.

#### <a name="tested"></a> Tested Platforms

<table>
<tbody>
<tr><th>Firebird version</th><th>Operating System</th><th>Compiler</th></tr>
<tr><td>1.5.2.4731</td><td>SunOS 5.10</td><td>g++ 3.4.3</td></tr>
<tr><td>1.5.2.4731</td><td>Windows XP</td><td>Visual C++ 8.0</td></tr>
<tr><td>1.5.3.4870</td><td>Windows XP</td><td>Visual C++ 8.0 Professional</td></tr>
<tr><td>2.5.2.26540</td><td>Debian GNU/Linux 7</td><td>g++ 4.7.2</td></tr>
</tbody>
</table>

#### <a name="required"></a> Required Client Libraries

The Firebird backend requires Firebird's `libfbclient` client library.

### <a name="connecting"></a> Connecting to the Database

To establish a connection to a Firebird database, create a Session object using the firebird backend factory together with a connection string:

    BackEndFactory const &backEnd = firebird;
    Session sql(backEnd,
            "service=/usr/local/firbird/db/test.fdb user=SYSDBA password=masterkey");

or simply:

    Session sql(firebird,
            "service=/usr/local/firbird/db/test.fdb user=SYSDBA password=masterkey");

The set of parameters used in the connection string for Firebird is:

* service
* user
* password
* role
* charset

The following parameters have to be provided as part of the connection string : *service*, *user*, *password*. Role and charset parameters are optional.

Once you have created a `Session` object as shown above, you can use it to access the database, for example:

    int count;
    sql << "select count(*) from user_tables", into(count);

(See the [SOCI basics](../basics.html) and [exchanging data](../exchange.html) documentation for general information on using the `Session` class.)


### <a name="features"></a> SOCI Feature Support

#### <a name="dynamic"></a> Dynamic Binding

The Firebird backend supports the use of the SOCI `Row` class, which facilitates retrieval of data whose type is not known at compile time.

When calling `Row::get<T>()`, the type you should pass as T depends upon the underlying database type. For the Firebird backend, this type mapping is:

<table>
  <tbody>
    <tr>
      <th>Firebird Data Type</th>
      <th>SOCI Data Type</th>
      <th><code>Row::get&lt;T&gt;</code> specializations</th>
    </tr>
    <tr>
      <td>numeric, decimal <br />*(where scale &gt; 0)*</td>
      <td><code>eDouble</code></td>
      <td><code>double</code></td>
    </tr>
    <tr>
      <td>numeric, decimal <sup>[<a href="#note1">1</a>]</sup><br />*(where scale = 0)*</td>
      <td><code>eInteger, eDouble</code></td>
      <td><code>int, double</code></td>
    </tr>
    <tr>
      <td>double precision, float</td>
      <td><code>eDouble</code></td>
      <td><code>double</code></td>
    </tr>
    <tr>
      <td>smallint, integer</td>
      <td><code>eInteger</code></td>
      <td><code>int</code></td>
    </tr>
    <tr>
      <td>char, varchar</td>
      <td><code>eString</code></td>
      <td><code>std::string</code></td>
    </tr>
    <tr>
      <td>date, time, timestamp</td>
      <td><code>eDate</code></td>
      <td><code>std::tm</code></code></code></td>
    </tr>
  </tbody>
</table>

<a name="note1" />&nbsp;<sup>[1]</sup> &nbsp;There is also 64bit integer type for larger values which is
currently not supported.

(See the [dynamic resultset binding](../exchange.html#dynamic) documentation for general information on using the `Row` class.)


#### <a name="binding"></a> Binding by Name

In addition to [binding by position](../exchange.html#bind_position), the Firebird backend supports [binding by name](../exchange.html#bind_name), via an overload of the `use()` function:

    int id = 7;
    sql << "select name from person where id = :id", use(id, "id")

It should be noted that parameter binding by name is supported only by means of emulation, since the underlying API used by the backend doesn't provide this feature.

#### <a name="bulk"></a> Bulk Operations

The Firebird backend has full support for SOCI's [bulk operations](../statements.html#bulk) interface. This feature is also supported by emulation.

#### <a name="transactions"></a> Transactions

[Transactions](../statements.html#transactions) are also fully supported by the Firebird backend. In fact, an implicit transaction is always started when using this backend if one hadn't been started by explicitly calling <tt>begin()</tt> before. The current transaction is automatically committed in `Session's` destructor.

#### <a name="blob"></a> BLOB Data Type

The Firebird backend supports working with data stored in columns of type Blob, via SOCI's `[BLOB](../exchange.html#blob)` class.

It should by noted, that entire Blob data is fetched from database to allow random read and write access. This is because Firebird itself allows only writing to a new Blob or reading from existing one - modifications of existing Blob means creating a new one. Firebird backend hides those details from user.

#### <a name="rowid"></a> RowID Data Type

This feature is not supported by Firebird backend.

#### <a name="nested"></a> Nested Statements

This feature is not supported by Firebird backend.

#### <a name="stored"></a> Stored Procedures

Firebird stored procedures can be executed by using SOCI's [Procedure](../statements.html#procedures) class.


### <a name="native"></a> Acessing the native database API

SOCI provides access to underlying datbabase APIs via several getBackEnd() functions, as described in the [beyond SOCI](../beyond.html) documentation.

The Firebird backend provides the following concrete classes for navite API access:

<table>
  <tbody>
    <tr>
      <th>Accessor Function</th>
      <th>Concrete Class</th>
    </tr>
    <tr>
      <td><code>SessionBackEnd* Session::getBackEnd()</code></td>
      <td><code>FirebirdSessionBackEnd</code></td>
    </tr>
    <tr>
      <td><code>StatementBackEnd* Statement::getBackEnd()</code></td>
      <td><code>FirebirdStatementBackEnd</code></td>
    </tr>
    <tr>
      <td><code>BLOBBackEnd* BLOB::getBackEnd()</code></td>
      <td><code>FirebirdBLOBBackEnd</code></td>
    </tr>
    <tr>
      <td><code>RowIDBackEnd* RowID::getBackEnd()</code></td>
      <td<>code>FirebirdRowIDBackEnd</code></td>
    </tr>
  </tbody>
</table>

### <a name="extensions"></a> Backend-specific extensions

#### <a name="firebirdsocierror"></a> FirebirdSOCIError

The Firebird backend can throw instances of class `FirebirdSOCIError`, which is publicly derived from `SOCIError` and has an additional public `status_` member containing the Firebird status vector.