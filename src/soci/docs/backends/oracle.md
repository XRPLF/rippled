## Oracle Backend Reference

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
    * [oracle_soci_error](#oraclesocierror)


### <a name="prerequisites"></a> Prerequisites
 
#### <a name="versions"></a> Supported Versions

The SOCI Oracle backend is currently supported for use with Oracle 10 or later.
Older versions of Oracle may work as well, but they have not been tested by the SOCI team.

#### <a name="tested"></a> Tested Platforms

<table>
<tbody>
<tr><th>Oracle version</th><th>Operating System</th><th>Compiler</th></tr>
<tr><td>10.2.0 (XE)</td><td>RedHat 5</td><td>g++ 4.3</td></tr>
</tbody>
</table>


#### <a name="required"></a> Required Client Libraries

The SOCI Oracle backend requires Oracle's `libclntsh` client library. Depending on the particular system, the `libnnz10` library might be needed as well.

Note that the SOCI library itself depends also on `libdl`, so the minimum set of libraries needed to compile a basic client program is:

    -lsoci_core -lsoci_oracle -ldl -lclntsh -lnnz10

#### <a name="connecting"></a> Connecting to the Database

To establish a connection to an Oracle database, create a `session` object using the oracle backend factory together with a connection string:

    session sql(oracle, "service=orcl user=scott password=tiger");

    // or:
    session sql("oracle", "service=orcl user=scott password=tiger");

    // or:
    session sql("oracle://service=orcl user=scott password=tiger");

The set of parameters used in the connection string for Oracle is:

* `service`
* `user`
* `password`
* `mode` (optional)

Once you have created a `session` object as shown above, you can use it to access the database, for example:

    int count;
    sql << "select count(*) from user_tables", into(count);

(See the [SOCI basics](../basics.html) and [exchanging data](../exchange.html) documentation for general information on using the `session` class.)#

### <a name="features"></a> SOCI Feature Support

#### <a name="dynamic"></a> Dynamic Binding

The Oracle backend supports the use of the SOCI `row` class, which facilitates retrieval of data which type is not known at compile time.

When calling `row::get<T>()`, the type you should pass as `T` depends upon the nderlying database type.<br/>  For the Oracle backend, this type mapping is:

<table>
  <tbody>
    <tr>
      <th>Oracle Data Type</th>
      <th>SOCI Data Type</th>
      <th><code>row::get&lt;T&gt;</code> specializations</th>
    </tr>
    <tr>
      <td>number <i>(where scale &gt; 0)</i></td>
      <td><code>dt_double</code></td>
      <td><code>double</code></td>
    </tr>
    <tr>
      <td>number<br /><i>(where scale = 0 and precision &le; std::numeric_limits&lt;int&gt;::digits10)</i></td>
      <td><code>dt_integer</code></td>
      <td><code>int</code></td>
    </tr>
    <tr>
      <td>number</td>
      <td><code>dt_long_long</code></td>
      <td><code>long long</code></td>
    </tr>
    <tr>
      <td>char, varchar, varchar2</td>
      <td><code>dt_string</code></td>
      <td><code>std::string</code></td>
    </tr>
    <tr>
      <td>date</td>
      <td><code>dt_date</code></td>
      <td><code>std::tm</code></td>
    </tr>
  </tbody>
</table>


(See the [dynamic resultset binding](../exchange.html#dynamic) documentation for general information on using the `row` class.)

#### <a name="name"></a> Binding by Name

In addition to [binding by position](../exchange.html#bind_position), the Oracle backend supports [binding by name](../exchange.html#bind_name), via an overload of the `use()` function:

    int id = 7;
    sql << "select name from person where id = :id", use(id, "id")

SOCI's use of ':' to indicate a value to be bound within a SQL string is consistant with the underlying Oracle client library syntax.

#### <a name="bulk"></a> Bulk Operations

The Oracle backend has full support for SOCI's [bulk operations](../statements.html#bulk) interface.

#### <a name="transactions"></a> Transactions

[Transactions](../statements.html#transactions) are also fully supported by the Oracle backend,
although transactions with non-default isolation levels have to be managed by explicit SQL statements.

#### <a name="blob"></a> blob Data Type

The Oracle backend supports working with data stored in columns of type Blob, via SOCI's [blob](../exchange.html#blob) class.

#### <a name="rowid"></a> rowid Data Type

Oracle rowid's are accessible via SOCI's [rowid](../reference.html#rowid) class.

#### <a name="nested"></a> Nested Statements

The Oracle backend supports selecting into objects of type `statement`, so that you may work with nested sql statements and PL/SQL cursors:

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

#### <a name="stored"></a> Stored Procedures

Oracle stored procedures can be executed by using SOCI's [procedure](../statements.html#procedures) class.

### <a name="native"></a> Acessing the native database API

SOCI provides access to underlying datbabase APIs via several `get_backend()` functions, as described in the [Beyond SOCI](../beyond.html) documentation.

The Oracle backend provides the following concrete classes for navite API access:

<table>
  <tbody>
    <tr>
      <th>Accessor Function</th>
      <th>Concrete Class</th>
    </tr>
    <tr>
      <td><code>session_backend * session::get_backend()</code></td>
      <td><code>oracle_session_backend</code></td>
    </tr>
    <tr>
      <td><code>statement_backend * statement::get_backend()</code></td>
      <td><code>oracle_statement_backend</code></td>
    </tr>
    <tr>
      <td><code>blob_backend * blob::get_backend()</code></td>
      <td><code>oracle_blob_backend</code></td>
    </tr>
    <tr>
      <td><code>rowid_backend * rowid::get_backend()</code></td>
      <td><code>oracle_rowid_backend</code></td>
    </tr>
  </tbody>
</table>

### <a name="extensions"></a> Backend-specific extensions

#### <a name="oraclesocierror"></a> oracle_soci_error

The Oracle backend can throw instances of class `oracle_soci_error`, which is publicly derived from `soci_error` and has an additional public `err_num_` member containing the Oracle error code:

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