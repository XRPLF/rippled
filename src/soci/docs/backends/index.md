## Existing backends and supported platforms

### Supported Features

(Follow the links to learn more about each backend.)

<table>
  <tbody>
    <tr>
      <th></th>
      <th><a href="oracle.html">Oracle</a></th>
      <th><a href="postgresql.html">PostgreSQL</a></th>
      <th><a href="mysql.html">MySQL</a></th>
      <th><a href="sqlite3.html">SQLite3</a></th>
      <th><a href="firebird.html">Firebird</a></th>
      <th><a href="odbc.html">ODBC</a></th>
      <th><a href="db2.html">DB2</a></th>
    </tr>
    <tr>
      <td>Binding by Name</td>
      <td>YES</td>
      <td><a href="postgresql.html#bindingbyname">YES (>=8.0)</a></td>
      <td><a href="mysql.html#bindingbyname">YES</a></td>
      <td>YES</td>
      <td><a href="firebird.html#bindingbyname">YES</a></td>
      <td>YES</td>
      <td>YES</td>
    </tr>
    <tr>
      <td>Dynamic Binding</td>
      <td><a href="oracle.html#dynamic">YES</a></td>
      <td><a href="postgresql.html#dynamic">YES</a></td>
      <td><a href="mysql.html#dynamic">YES</a></td>
      <td>YES</td>
      <td>YES</td>
      <td>YES</td>
      <td></td>
    </tr>
    <tr>
      <td>Bulk Operations</td>
      <td>YES</td>
      <td>YES</td>
      <td>YES</td>
      <td>YES</td>
      <td><a href="firebird.html#bulk">YES</a></td>
      <td>YES</td>
      <td>YES</td>
    </tr>
    <tr>
      <td>Transactions</td>
      <td>YES</td>
      <td>YES</td>
      <td><a href="mysql.html#transactions">YES</a>
        (with servers that support them, usually >=&nbsp;4.0)</td>
      <td>YES</td>
      <td><a href="firebird.html#transactions">YES</a></td>
      <td>YES</td>
      <td>YES</td>
    </tr>
    <tr>
      <td>BLOB Data Type</td>
      <td>YES</td>
      <td><a href="postgresql.html#blob">YES</a></td>
      <td>MySQL's BLOB type is mapped to <code>std::string</code></td>
      <td>YES</td>
      <td><a href="firebird.html#blob">YES</a></td>
      <td>NO</td>
      <td>NO</td>
    </tr>
    <tr>
      <td>RowID Data Type</td>
      <td>YES</td>
      <td>YES</td>
      <td>NO</td>
      <td>NO</td>
      <td>NO</td>
      <td>NO</td>
      <td>NO</td>
    </tr>
    <tr>
      <td>Nested Statements</td>
      <td>YES</td>
      <td>NO</td>
      <td>NO</td>
      <td>NO</td>
      <td>NO</td>
      <td>NO</td>
      <td>YES</td>
    </tr>
    <tr>
      <td>Stored Procedures</td>
      <td>YES</td>
      <td>YES</td>
      <td><a href="mysql.html#procedures">NO (but stored functions, YES)</a></td>
      <td>NO</td>
      <td>YES</td>
      <td>NO</td>
      <td>YES</td>
    </tr>
  </tbody>
</table>
