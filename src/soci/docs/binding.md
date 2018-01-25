# Data Binding

SOCI provides mechanisms to bind local buffers for input and output data.

**Note:** The Oracle documentation uses two terms: *defining* (for instructing the library where the *output* data should go) and *binding* (for the *input* data and *input/output* PL/SQL parameters). For the sake of simplicity, SOCI uses the term *binding* for both of these.

## Binding output data (into)

The `into` expression is used to add binding information to
the statement:

```cpp
int count;
sql << "select count(*) from person", into(count);

string name;
sql << "select name from person where id = 7", into(name);
```

In the above examples, some data is retrieved from the database and transmitted *into* the given local variable.

There should be as many `into` elements as there are expected columns in the result (see [dynamic resultset binding](#dynamic") for the exception to this rule).

## Binding input data (use)

The `use` expression associates the SQL placeholder (written with colon) with the local data:

```cpp
int val = 7;
sql << "insert into numbers(val) values(:val)", use(val);
```

In the above statement, the first "val" is a column name (assuming that there is appropriate table `numbers` with this column), the second "val" (with colon) is a placeholder and its name is ignored here, and the third "val" is a name of local variable.

To better understand the meaning of each "val" above, consider also:

```cpp
int number = 7;
sql << "insert into numbers(val) values(:blabla)", use(number);
```

Both examples above will insert the value of some local variable into the table `numbers` - we say that the local variable is *used* in the SQL statement.

There should be as many `use` elements as there are parameters used in the SQL query.

### Object lifetime and immutability

SOCI assumes that local variables provided as `use` elements live at least as long at it takes to execute the whole statement.
In short statement forms like above, the statement is executed *sometime* at the end of the full expression and the whole process is driven by the invisible temporary object handled by the library.
If the data provided by user comes from another temporary variable, it might be possible for the compiler to arrange them in a way that the user data will be destroyed *before* the statement will have its chance to execute, referencing objects that no longer exist:

```cpp
// Dangerous code!

string getNameFromSomewhere();

sql << "insert into person(name) values(:n)", use(getNameFromSomewhere());
```

In the above example, the data passed to the database comes from the temporary variable that is a result of call to `getNameFromSomewhere` - this should be avoided and named variables should be used to ensure safe lifetime relations:

```cpp
// Safe code

string getNameFromSomewhere();

string name = getNameFromSomewhere();
sql << "insert into person(name) values(:n)", use(name);
```

It is still possible to provide `const` data for use elements.

Note that some database servers, like Oracle, allow PL/SQL procedures to modify their in/out parameters - this is detected by the SOCI library and an error is reported if the database attempts to modify the `use` element that holds `const` data.

The above example can be ultimately written in the following way:

```cpp
// Safe and efficient code

string getNameFromSomewhere();

string const& name = getNameFromSomewhere();
sql << "insert into person(name) values(:n)", use(name);
```

### Portability note

Older versions of the PostgreSQL client API do not allow to use input parameters at all.
In order to compile SOCI with those old client libraries, define the `SOCI_POSTGRESQL_NOPARAMS` preprocessor name passing `-DSOCI_POSTGRESQL_NOPARAMS=ON` variable to CMake.

## Binding by position

If there is more output or input "holes" in the single statement, it is possible to use many `into` and `use` expressions, separated by commas, where each expression will be responsible for the consecutive "hole" in the statement:

```cpp
string firstName = "John", lastName = "Smith";
int personId = 7;

sql << "insert into person(id, firstname, lastname) values(:id, :fn, :ln)",
        use(personId), use(firstName), use(lastName);

sql << "select firstname, lastname from person where id = :id",
        into(firstName), into(lastName), use(personId);
```

In the code above, the order of "holes" in the SQL statement and the order of `into` and `use` expression should match.

## Binding by name

The SQL placeholders that have their names (with colon) can be bound by name to clearly associate the local variable with the given placeholder.

This explicit naming allows to use different order of elements:

```cpp
string firstName = "John", lastName = "Smith";
int personId = 7;
sql << "insert into person(id, firstname, lastname) values(:id, :fn, :ln)",
    use(firstName, "fn"), use(lastName, "ln"), use(personId, "id");
```

or bind the same local data to many "holes" at the same time:

```cpp
string addr = "...";
sql << "update person"
        " set mainaddress = :addr, contactaddress = :addr"
        " where id = 7",
        use(addr, "addr");
```

### Portability notes

The PostgreSQL backend allows to use the "native" PostgreSQL way of naming parameters in the query, which is by numbers like `$1`, `$2`, `$3`, etc.
In fact, the backend *rewrites* the given query to the native form - and this is also one of the very few places where SOCI intrudes into the SQL query.
For portability reasons, it is recommended to use named parameters, as shown in the examples above.

The query rewriting can be switched off by compiling the backend with the `SOCI_POSTGRESQL_NOBINDBYNAME` name defined (pass `-DSOCI_POSTGRESQL_NOBINDBYNAME=ON` variable to CMake).
Note that in this case it is also necessary to define `SOCI_POSTGRESQL_NOPREPARE` (controlled by CMake variable `-DSOCI_POSTGRESQL_NOPREPARE=ON`), because statement preparation relies on successful query rewriting.

In practice, both macros will be needed for PostgreSQL server older than 8.0.

## Bulk operations

Bulk operations allow the user to bind, as into or use element, whole vectors of objects.
This allows the database backend to optimize access and data transfer and benefit from the fact that `std::vector` stores data in contiguous memory blocks (the actual optimization depends on the backend and the capability of the underlying data base server).

It is possible to `use` the vector as a data source:

```cpp
std::vector<int> v;
// ...
sql << "insert into t ...", use(v);
```

as well as a destination:

```cpp
std::vector<int> v;
v.resize(100);
sql << "select ...", into(v);
```

In the latter case the initial size of the vector defines the maximum number of data elements that the user is willing to accept and after executing the query the vector will be automatically resized to reflect that actual number of rows that were read and transmitted.
That is, the vector will be automatically shrunk if the amount of data that was available was smaller than requested.

It is also possible to operate on the chosen sub-range of the vector:

```cpp
std::vector<int> v;
// ...
std::size_t begin = ...;
std::size_t end = ...;
sql << "insert into t ...", use(v, begin, end);

// or:

sql << "select ...", into(v, begin, end);
```

Above, only the sub-range of the vector is used for data transfer and in the case of `into` operation, the `end` variable will be automatically adjusted to reflect the amount of data that was actually transmitted, but the vector object as a whole will retain its initial size.

Bulk operations can also involve indicators, see below.

Bulk operations support user-defined data types, if they have appropriate conversion routines defined.
