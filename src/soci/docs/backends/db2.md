## DB2 Backend Reference

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
* [Configuration options](#config)

### <a name="prerequisites"></a> Prerequisites
#### <a name="versions"></a> Supported Versions

The SOCI DB2 backend.

#### <a name="platforms"></a> Tested Platforms

<table>
<tbody>
<tr><th>DB2 version</th><th>Operating System</th><th>Compiler</th></tr>
<tr><td>-</td><td>Linux PPC64</td><td>GCC</td></tr>
<tr><td>9.1</td><td>Linux</td><td>GCC</td></tr>
<tr><td>9.5</td><td>Linux</td><td>GCC</td></tr>
<tr><td>9.7</td><td>Linux</td><td>GCC</td></tr>
<tr><td>10.1</td><td>Linux</td><td>GCC</td></tr>
<tr><td>10.1</td><td>Windows 8</td><td>Visual Studio 2012</td></tr>
</tbody>
</table>

#### <a name="required"></a> Required Client Libraries

The SOCI DB2 backend requires IBM DB2 Call Level Interface (CLI) library.

#### <a name="connecting"></a> Connecting to the Database

On Unix, before using the DB2 backend please make sure, that you have sourced DB2 profile into your environment:

    . ~/db2inst1/sqllib/db2profile

To establish a connection to the DB2 database, create a session object using the <code>DB2</code> backend factory together with the database file name:

    soci::session sql(soci::db2, "your DB2 connection string here");

### <a name="features"></a> SOCI Feature Support

#### <a name="dynamic"></a> Dynamic Binding

TODO

#### <a name="bulk"></a> Bulk Operations

Supported, but with caution as it hasn't been extensively tested.

#### <a name="transactions"></a> Transactions

Currently, not supported.

#### <a name="blob"></a> BLOB Data Type

Currently, not supported.

#### <a name="nested"></a> Nested Statements

Nesting statements are not processed by SOCI in any special way and they work as implemented by the DB2 database.

#### <a name="stored"></a> Stored Procedures

Stored procedures are supported, with <code>CALL</code> statement.

### <a name="native"></a> Acessing the native database API

TODO

### <a name="backend"></a> Backend-specific extensions

None.

### <a name="config"></a> Configuration options

None.