## ODBC Backend Reference

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
    * [odbc_soci_error](#odbcsocierror)
* [Configuration options](#configuration)


### <a name="prerequisites"></a> Prerequisites

#### <a name="versions"></a> Supported Versions

The SOCI ODBC backend is supported for use with ODBC 3.

#### <a name="tested"></a> Tested Platforms

<table>
<tbody>
<tr><th>ODBC version</th><th>Operating System</th><th>Compiler</th></tr>
<tr><td>3</td><td>Linux (Ubuntu 12.04)</td><td>g++ 4.6.3</td></tr>
<tr><td>3</td><td>Linux (Ubuntu 12.04)</td><td>clang 3.2</td></tr>
<tr><td>3.8</td><td>Windows 8</td><td>Visual Studio 2012</td></tr>
<tr><td>3</td><td>Windows 7</td><td>Visual Studio 2010</td></tr>
<tr><td>3</td><td>Windows XP</td><td>Visual Studio 2005 (express)</td></tr>
<tr><td>3</td><td>Windows XP</td><td>Visual C++ 8.0 Professional</td></tr>
<tr><td>3</td><td>Windows XP</td><td>g++ 3.3.4 (Cygwin)</td></tr>
</tbody>
</table>

#### <a name="required"></a> Required Client Libraries

The SOCI ODBC backend requires the ODBC client library.

### <a name="connecting"></a> Connecting to the Database

To establish a connection to the ODBC database, create a Session object using the `ODBC` backend factory together with a connection string:

    backend_factory const&amp; backEnd = odbc;
    session sql(backEnd, "filedsn=c:\\my.dsn");

or simply:

    session sql(odbc, "filedsn=c:\\my.dsn");

The set of parameters used in the connection string for ODBC is the same as accepted by the `[SQLDriverConnect](http://msdn.microsoft.com/library/default.asp?url=/library/en-us/odbcsql/od_odbc_d_4x4k.asp)` function from the ODBC library.

Once you have created a `session` object as shown above, you can use it to access the database, for example:

    int count;
    sql << "select count(*) from invoices", into(count);

(See the [SOCI basics](../basics.html) and [exchanging data](../exchange.html) documentation for general information on using the `session` class.)

### <a name="support"></a> SOCI Feature Support

#### <a name="dynamic"></a> Dynamic Binding

The ODBC backend supports the use of the SOCI `row` class, which facilitates retrieval of data whose type is not known at compile time.

When calling `row::get<T>()`, the type you should pass as T depends upon the underlying database type.
For the ODBC backend, this type mapping is:

<table>
  <tbody>
    <tr>
      <th>ODBC Data Type</th>
      <th>SOCI Data Type</th>
      <th><code>row::get&lt;T&gt;</code> specializations</th>
    </tr>
    <tr>
      <td>SQL_DOUBLE
      , SQL_DECIMAL
      , SQL_REAL
      , SQL_FLOAT
      , SQL_NUMERIC
    </td>
      <td><code>dt_double</code></td>
      <td><code>double</code></td>
    </tr>
    <tr>
      <td>SQL_TINYINT
      , SQL_SMALLINT
      , SQL_INTEGER
      , SQL_BIGINT</td>
      <td><code>dt_integer</code></td>
      <td><code>int</code></td>
    </tr>
    <tr>
      <td>SQL_CHAR, SQL_VARCHAR</td>
      <td><code>dt_string</code></td>
      <td><code>std::string</code></td>
    </tr>
    <tr>
      <td>SQL_TYPE_DATE
      , SQL_TYPE_TIME
      , SQL_TYPE_TIMESTAMP</td>
      <td><code>dt_date</code></td>
      <td><code>std::tm</code></code></code></td>
    </tr>
  </tbody>
</table>

Not all ODBC drivers support all datatypes.

(See the [dynamic resultset binding](../exchange.html#dynamic) documentation for general information on using the `row` class.)


#### <a name="name"></a>  Binding by Name

In addition to [binding by position](../exchange.html#bind_position), the ODBC backend supports [binding by name](../exchange.html#bind_name), via an overload of the `use()` function:

    int id = 7;
    sql << "select name from person where id = :id", use(id, "id")

Apart from the portable "colon-name" syntax above, which is achieved by rewriting the query string, the backend also supports the ODBC ? syntax:

    int i = 7;
    int j = 8;
    sql << "insert into t(x, y) values(?, ?)", use(i), use(j);

#### <a name="bulk"></a> Bulk Operations

The ODBC backend has support for SOCI's [bulk operations](../statements.html#bulk) interface.  Not all ODBC drivers support bulk operations, the following is a list of some tested backends:

<table>
  <tbody>
    <tr>
      <th>ODBC Driver</th>
      <th>Bulk Read</th>
      <th>Bulk Insert</th>
    </tr>
    <tr>
    <td>MS SQL Server 2005</td>
    <td>YES</td>
    <td>YES</td>
  </tr>
    <tr>
    <td>MS Access 2003</td>
    <td>YES</td>
    <td>NO</td>
  </tr>
    <tr>
    <td>PostgresQL 8.1</td>
    <td>YES</td>
    <td>YES</td>
  </tr>
    <tr>
    <td>MySQL 4.1</td>
    <td>NO</td>
    <td>NO</td>
  </tr>
  </tbody>
</table>

#### <a name="transactions"></a> Transactions

[Transactions](../statements.html#transactions) are also fully supported by the ODBC backend, provided that they are supported by the underlying database.

#### BLOB Data Type

Not currently supported.

#### RowID Data Type

Not currently supported.

#### Nested Statements

Not currently supported.

#### Stored Procedures

Not currently supported.

### <a name="native"></a> Acessing the native database API

SOCI provides access to underlying datbabase APIs via several getBackEnd() functions, as described in the [beyond SOCI](../beyond.html) documentation.

The ODBC backend provides the following concrete classes for navite API access:

<table>
  <tbody>
    <tr>
      <th>Accessor Function</th>
      <th>Concrete Class</th>
    </tr>
    <tr>
      <td><code>session_backend* session::get_backend()</code></td>
      <td><code>odbc_statement_backend</code></td>
    </tr>
    <tr>
      <td><code>statement_backend* statement::get_backend()</code></td>
      <td><code>odbc_statement_backend</code></td>
    </tr>
    <tr>
      <td><code>rowid_backend* rowid::get_backend()</code></td>
      <td><code>odbc_rowid_backend</code></td>
    </tr>
  </tbody>
</table>

### <a name="extensions"></a> Backend-specific extensions

#### <a name="odbcsocierror"></a> odbc_soci_error

The ODBC backend can throw instances of class `odbc_soci_error`, which is publicly derived from `soci_error` and has additional public members containing the ODBC error code, the Native database error code, and the message returned from ODBC:

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

#### <a name="getconenctionstring"></a> get_connection_string()

The `odbc_session_backend` class provides `std::string get_connection_string() const` method
that returns fully expanded connection string as returned by the `SQLDriverConnect` function.

### <a name="configuration"></a> Configuration options

This backend supports `odbc_option_driver_complete` option which can be passed to it via `connection_parameters` class. The value of this option is passed to `SQLDriverConnect()` function as "driver completion" parameter and so must be one of `SQL_DRIVER_XXX` values, in the string form. The default value of this option is `SQL_DRIVER_PROMPT` meaning that the driver will query the user for the user name and/or the password if they are not stored together with the connection. If this is undesirable for some reason, you can use `SQL_DRIVER_NOPROMPT` value for this option to suppress showing the message box:

    connection_parameters parameters("odbc", "DSN=mydb");
    parameters.set_option(odbc_option_driver_complete, "0" /* SQL_DRIVER_NOPROMPT */);
    session sql(parameters);