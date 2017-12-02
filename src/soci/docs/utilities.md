# Utilities

SOCI provides a portable abstraction for selection of database queries.

## DDL

SOCI supports some basic methods to construct portable DDL queries. That is, instead of writing explicit SQL statement for creating or modifying tables, it is possible to use dedicated SOCI functions, which prepare appropriate DDL statements behind the scenes, thus enabling the user application to create basic database structures in a way that is portable across different database servers. Note that the actual support for these functions depends on the actual backend implementation.

It is possible to create a new table in a single statement:

```cpp
sql.create_table("t1").column("i", soci::dt_integer).column("j", soci::dt_integer);
```

Above, table "t1" will be created with two columns ("i", "j") of type integer.

It is also possible to build similar statements piece by piece, which is useful if the table structure is computed dynamically:

```cpp
{
    soci::ddl_type ddl = sql.create_table("t2");
    ddl.column("i", soci::dt_integer);
    ddl.column("j", soci::dt_integer);
    ddl.column("k", soci::dt_integer)("not null");
    ddl.primary_key("t2_pk", "j");
}
```

The actual statement is executed at the end of above block, when the ddl object goes out of scope. The "not null" constraint was added to the definition of column "k" explicitly and in fact any piece of SQL can be inserted this way - with the obvious caveat of having limited portability (the "not null" piece seems to be universaly portable).

Columns can be added to and dropped from already existing tables as well:

```cpp
sql.add_column("t1", "k", soci::dt_integer);
// or with constraint:
//sql.add_column("t1", "k", soci::dt_integer)("not null");

sql.drop_column("t1", "i");
```

If needed, precision and scale can be defined with additional integer arguments to functions that create columns:

```cpp
sql.add_column("t1", "s", soci::dt_string, precision);
sql.add_column("t1", "d", soci::dt_double, precision, scale);
```

Tables with foreign keys to each other can be also created:

```cpp
{
    soci::ddl_type ddl = sql.create_table("t3");
    ddl.column("x", soci::dt_integer);
    ddl.column("y", soci::dt_integer);
    ddl.foreign_key("t3_fk", "x", "t2", "j");
}
```

Tables can be dropped, too:

```cpp
sql.drop_table("t1");
sql.drop_table("t3");
sql.drop_table("t2");
```

Note that due to the differences in the set of types that are actually supported on the target database server, the type mappings, as well as precision and scales, might be different, even in the way that makes them impossible to portably recover with metadata queries.

In the category of portability utilities, the following functions are also available:

```cpp
sql.empty_blob()
```

the above call returns the string containing expression that represents an empty BLOB value in the given target backend. This expression can be used as part of a bigger SQL statement, for example:

```cpp
sql << "insert into my_table (x) values (" + sql.empty_blob() + ")";
```

and:

```cpp
sql.nvl()
```

the above call returns the string containing the name of the SQL function that implements the NVL or COALESCE operation in the given target backend, for example:

```cpp
sql << "select name, " + sql.nvl() + "(phone, \'UNKNOWN\') from phone_book";
```

Note: `empty_blob` and `nvl` are implemented in Oracle, PostgreSQL and SQLite3 backends; for other backends their behaviour is as for PostgreSQL.

## DML

Only two related functions are currently available in this category:
`get_dummy_from_clause()` can be used to construct select statements that don't
operate on any table in a portable way, as while some databases allow simply
omitting the from clause in this case, others -- e.g. Oracle -- still require
providing some syntactically valid from clause even if it is not used. To use
this function, simply append the result of this function to the statement:

```cpp
double databasePi;
session << ("select 4*atan(1)" + session.get_dummy_from_clause()),
            into(databasePi);
```

If just the name of the dummy table is needed, and not the full clause, you can
use `get_dummy_from_table()` to obtain it.

Notice that both functions require the session to be connected as their result
depends on the database it is connected to.

## Database Metadata

It is possible to portably query the database server to obtain basic metadata information.

In order to get the list of table names in the current schema:

```cpp
std::vector<std::string> names(100);
sql.get_table_names(), into(names);
```

alternatively:

```cpp
std::string name;
soci::statement st = (sql.prepare_table_names(), into(name));

st.execute();
while (st.fetch())
{
    // ...
}
```

Similarly, to get the description of all columns in the given table:

```cpp
soci::column_info ci;
soci::statement st = (sql.prepare_column_descriptions(table_name), into(ci));

st.execute();
while (st.fetch())
{
    // ci fields describe each column in turn
}
```
