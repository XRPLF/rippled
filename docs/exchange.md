## Exchanging data

* [Binding local data](#bind_local)
* [Binding output data](#bind_output)
    * [Binding input data](#bind_input)
    * [Binding by position](#bind_position)
    * [Binding by name](#bind_name)
* [Handling of nulls and other conditions](exchange.html#data_states)
    * [Indicators](#indicators)
* [Types](#types)
    * [Static binding](#static)
    * [Static binding for bulk operations](#static_bulk)  
    * [Dynamic resultset binding](#dynamic)
    * [Extending with user-provided datatypes](#custom_types)
    * [Object-relational mapping](#object_relational)
* [Large objects (BLOBs)](#blob)


### <a name="bind_local"></a> Binding local data

##### Note:

The Oracle documentation uses two terms: *defining* (for instructing the library where the *output* data should go) and *binding* (for the *input* data and *input/output* PL/SQL parameters). For the sake of simplicity, SOCI uses the term *binding* for both of these.

    int count;
    sql << "select count(*) from person", into(count);

    string name;
    sql << "select name from person where id = 7", into(name);

In the above examples, some data is retrieved from the database and transmitted *into* the given local variable.

There should be as many `into` elements as there are expected columns in the result (see [dynamic resultset binding](#dynamic) for the exception to this rule).


#### <a name="bind_local"></a> Binding output

The `into` expression is used to add binding information to
the statement:

    int count;
    sql << "select count(*) from person", into(count);

    string name;
    sql << "select name from person where id = 7", into(name);

In the above examples, some data is retrieved from the database and transmitted *into* the given local variable.


There should be as many `into` elements as there are expected columns in the result (see [dynamic resultset binding](#dynamic") for the exception to this rule).

#### <a name="bind_input"></a> Binding input data

The `use` expression associates the SQL placeholder (written with colon) with the local data:

    int val = 7;
    sql << "insert into numbers(val) values(:val)", use(val);

In the above statement, the first "val" is a column name (assuming that there is appropriate table `numbers` with this column), the second "val" (with colon) is a placeholder and its name is ignored here, and the third "val" is a name of local variable.

To better understand the meaning of each "val" above, consider also:

    int number = 7;
    sql << "insert into numbers(val) values(:blabla)", use(number);

Both examples above will insert the value of some local variable into the table `numbers` - we say that the local variable is *used* in the SQL statement.

There should be as many `use` elements as there are parameters used in the SQL query.

##### Portability note:

Older versions of the PostgreSQL client API do not allow to use input parameters at all. In order to compile SOCI with those old client libraries, define the `SOCI_POSTGRESQL_NOPARAMS` preprocessor name passing `-DSOCI_POSTGRESQL_NOPARAMS=ON` variable to CMake.

#### <a name="bind_position"></a> Binding by position

If there is more output or input "holes" in the single statement, it is possible to use many `into` and `use` expressions, separated by commas, where each expression will be responsible for the consecutive "hole" in the statement:

    string firstName = "John", lastName = "Smith";
    int personId = 7;

    sql << "insert into person(id, firstname, lastname) values(:id, :fn, :ln)",
        use(personId), use(firstName), use(lastName);

    sql << "select firstname, lastname from person where id = :id",
        into(firstName), into(lastName), use(personId);

In the code above, the order of "holes" in the SQL statement and the order of `into` and `use` expression should match.

#### <a name="bind_name"></a> Binding by name

The SQL placeholders that have their names (with colon) can be bound by name to clearly associate the local variable with the given placeholder.

This explicit naming allows to use different order of elements:

    string firstName = "John", lastName = "Smith";
    int personId = 7;
    sql << "insert into person(id, firstname, lastname) values(:id, :fn, :ln)",
        use(firstName, "fn"), use(lastName, "ln"), use(personId, "id");

or bind the same local data to many "holes" at the same time:

    string addr = "...";
    sql << "update person"
           " set mainaddress = :addr, contactaddress = :addr"
           " where id = 7",
           use(addr, "addr");

##### Object lifetime and immutability:

SOCI assumes that local variables provided as `use` elements live at least as long at it takes to execute the whole statement. In short statement forms like above, the statement is executed *sometime* at the end of the full expression and the whole process is driven by the invisible temporary object handled by the library. If the data provided by user comes from another temporary variable, it might be possible for the compiler to arrange them in a way that the user data will be destroyed *before* the statement will have its chance to execute, referencing objects that no longer exist:

    // dangerous code:

    string getNameFromSomewhere();

    sql << "insert into person(name) values(:n)",
        use(getNameFromSomewhere());

In the above example, the data passed to the database comes from the temporary variable that is a result of call to `getNameFromSomewhere` - this should be avoided and named variables should be used to ensure safe lifetime relations:

    // safe code:

    string getNameFromSomewhere();

    string name = getNameFromSomewhere();
    sql << "insert into person(name) values(:n)",
        use(name);

It is still possible to provide `const` data for use elements. Note that some database servers, like Oracle, allow PL/SQL procedures to modify their in/out parameters - this is detected by the SOCI library and an error is reported if the database attempts to modify the `use` element that holds `const`
data.

The above example can be ultimately written in the following way:

    // safe and efficient code:

    string getNameFromSomewhere();

    const string & name = getNameFromSomewhere();
    sql << "insert into person(name) values(:n)",
        use(name);

##### Portability notes:
The PostgreSQL backend allows to use the "native" PostgreSQL way of naming parameters in the query, which is by numbers like `$1`, `$2`, `$3`, etc. In fact, the backend *rewrites* the given query to the native form - and this is also one of the very few places where SOCI intrudes into the SQL query. For portability reasons, it is recommended to use named parameters, as shown in the examples above.

The query rewriting can be switched off by compiling the backend with the `SOCI_POSTGRESQL_NOBINDBYNAME` name defined (pass `-DSOCI_POSTGRESQL_NOBINDBYNAME=ON` variable to CMake). Note that in this case it is also necessary to define `SOCI_POSTGRESQL_NOPREPARE` (controlled by CMake variable `-DSOCI_POSTGRESQL_NOPREPARE=ON`), because statement preparation relies on successful query rewriting.
In practice, both macros will be needed for PostgreSQL server older than 8.0.

### <a name="data_states"></a> Handling nulls and other conditions
#### <a name="indicators"></a> Indicators
In order to support null values and other conditions which are not real errors, the concept of *indicator* is provided.

For example, when the following SQL query is executed:

    select name from person where id = 7

there are three possible outcomes:

1. there is a person with id = 7 and his name is returned
2. there is a person with id = 7, but he has no name (his name is null in the database table)
3. there is no such person

Whereas the first alternative is easy to handle, the other two are more complex. Moreover, they are not necessarily errors from the application's point of view and what's more interesting, they are *different* and the application may wish to detect which is the case.
The following example does this:

    string name;
    indicator ind;

    sql << "select name from person where id = 7", into(name, ind);

    if (sql.got_data())
    {
        switch (ind)
        {
        case i_ok:
            // the data was returned without problems
            break;
        case i_null:
            // there is a person, but he has no name (his name is null)
            break;
        case i_truncated:
            // the name was returned only in part,
            // because the provided buffer was too short
            // (not possible with std::string, but possible with char* and char[])
            break;
        }
    }
    else
    {
        // no such person in the database
    }

The use of indicator variable is optional, but if it is not used and the result would be `i_null`,
then the exception is thrown. This means that you should use indicator variables everywhere where the application logic (and database schema) allow the "attribute not set" condition.

Indicator variables can be also used when binding input data, to control whether the data is to be used as provided, or explicitly overrided to be null:

    int id = 7;
    string name;
    indicator ind = i_null;
    sql << "insert into person(id, name) values(:id, :name)",
        use(id), use(name, ind);

In the above example, the row is inserted with `name` attribute set to null.

Indicator variables can also be used in conjunction with vector based insert, update, and select statements:

    vector<string< names(100);
    vector<indicator< inds;
    sql << "select name from person where id = 7", into(names, inds);

The above example retrieves first 100 rows of data (or less). The initial size of `names` vector provides the (maximum) number of rows that should be read. Both vectors will be automatically resized according to the number of rows that were actually read.

The following example inserts null for each value of name:

    vector<int< ids;
    vector<string< names;
    vector<indicator< nameIndicators;

    for (int i = 0; i != 10; ++i)
    {
        ids.push_back(i);
        names.push_back("");
        nameIndicators.push_back(i_null);
    }

    sql << "insert into person(id, name) values(:id, :name)",
        use(ids), use(name, nameIndicators);

See also [Integration with Boost](boost.html) to learn how the Boost.Optional library can be used to handle null data conditions in a more natural way.

### <a name="types"></a> Types
#### <a name="static"></a> Static type binding

The static binding for types is most useful when the types used in the database are known at compile time - this was already presented above with the help of `into` and `use` functions.

The following types are currently supported for use with `into` and `use` expressions:

* `char` (for character values)
* `short`, `int`, `unsigned long`, `long long`, `double` (for numeric values)
* `char*`, `char[]`, `std::string` (for string values)
* `std::tm``` (for datetime values)
* `soci::statement` (for nested statements and PL/SQL cursors)
* `soci::blob` (for Binary Large OBjects)
* `soci::row_id` (for row identifiers)

See the test code that accompanies the library to see how each of these types is used.

#### <a name="static_bulk"></a> Static type binding for bulk operations

Bulk inserts, updates, and selects are supported through the following `std::vector` based into and use types:

*`std::vector<char<`
*`std::vector<short<`
*`std::vector<int<`
*`std::vector<unsigned long<`
*`std::vector<long long<`
*`std::vector<double<`
*`std::vector<std::string<`
*`std::vector<std::tm<`

Use of the vector based types mirrors that of the standard types, with the size of the vector used to specify the number of records to process at a time. See below for examples.

Note that bulk operations are supported only for `std::vector`s of the types listed above.

#### <a name="dynamic"></a> Dynamic resultset binding

For certain applications it is desirable to be able to select data from arbitrarily structured tables (e.g. via "`select * from ...`") and format the resulting data based upon its type. SOCI supports this through the `soci::row` and `soci::column_properties` classes.

Data is selected into a `row` object, which holds `column_properties` objects describing
the attributes of data contained in each column. Once the data type for each column is known, the data can be formatted appropriately.

For example, the code below creates an XML document from a selected row of data from an arbitrary table:

    row r;
    sql << "select * from some_table", into(r);

    std::ostringstream doc;
    doc << "<row<" << std::endl;
    for(std::size_t i = 0; i != r.size(); ++i)
    {
        const column_properties & props = r.get_properties(i);

        doc << '<' << props.get_name() << '<';

        switch(props.get_data_type())
        {
        case dt_string:
            doc << r.get<std::string<(i);
            break;
        case dt_double:
            doc << r.get<double<(i);
            break;
        case dt_integer:
            doc << r.get<int<(i);
            break;
        case dt_long_long:
            doc << r.get<long long<(i);
            break;
        case dt_unsigned_long_long:
            doc << r.get<unsigned long long<(i);
            break;
        case dt_date:
            std::tm when = r.get<std::tm<(i);
            doc << asctime(&when);
            break;
        }

        doc << "</" << props.get_name() << '<' << std::endl;
    }
    doc << "</row<";


The type `T` parameter that should be passed to `row::get<T<()` depends on the SOCI data type that is returned from `column_properties::get_data_type()`.

`row::get<T<()` throws an exception of type `std::bad_cast` if an incorrect type `T` is requested.

#####SOCI Data Type

`row::get<T<` specialization

*dt_double - double
*dt_integer - int
*dt_long_long - long long
*dt_unsigned_long_long - unsigned long long
*dt_string - std::string
*dt_date - std::tm

The mapping of underlying database column types to SOCI datatypes is database specific. See the [backend documentation](backends/index.html) for details.

The `row` also provides access to indicators for each column:

    row r;
    sql << "select name from some_table where id = 1", into(r);
    if (r.get_indicator(0) != soci::i_null)
    {
       std::cout << r.get<std::string<(0);
    }

It is also possible to extract data from the `row` object using its stream-like interface, where each extracted variable should have matching type respective to its position in the chain:

    row r;
    sql << "select name, address, age from persons where id = 123", into(r);

    string name, address;
    int age;

    r >> name >> address >> age;

Note, however, that this interface is *not* compatible with the standard `std::istream` class and that it is only possible to extract a single row at a time - for "safety" reasons the row boundary is preserved and it is necessary to perform the `fetch` operation explicitly for each consecutive row.


#### <a name="custom_types"></a> Extending SOCI to support custom (user-defined) C++ types

SOCI can be easily extended with support for user-defined datatypes.

The extension mechanism relies on appropriate specialization of the `type_conversion`
struct that converts to and from one of the following SOCI base types:

*`double`
*`int`
*`long long`
*`unsigned long long`
*`std::string`
*`char`
*`std::tm`

There are three required class members for a valid `type_conversion` specialization:

* the `base_type` type definition, aliasing either one of the base types *or another ser-defined type*
* the `from_base()` static member function, converting from the base type
* the `to_base()` static member function, converting to the base type

Note that no database-specific code is required to define user conversion.

The following example shows how the user can extend SOCI to support his own type `MyInt`, which here is some wrapper for the fundamental `int` type:

    class MyInt
    {
    public:
        MyInt() {}
        MyInt(int i) : i_(i) {}

        void set(int i) { i_ = i; }
        int get() const { return i_; }

    private:
        int i_;
    };

    namespace soci
    {
        template <<
        struct type_conversion<MyInt<
        {
            typedef int base_type;

            static void from_base(int i, indicator ind, MyInt & mi)
            {
                if (ind == i_null)
                {
                    throw soci_error("Null value not allowed for this type");
                }

                mi.set(i);
            }

            static void to_base(const MyInt & mi, int & i, indicator & ind)
            {
                i = mi.get();
                ind = i_ok;
            }
        };
    }

The above specialization for `soci::type_conversion<MyInt<` is enough to enable the following:

    MyInt i;

    sql << "select count(*) from person", into(i);

    cout << "We have " << i.get() << " persons in the database.\n";

Note that there is a number of types from the Boost library integrated with SOCI out of the box, see [Integration with Boost](boost.html) for complete description. Use these as examples of conversions for more complext data types.

Note also that user-defined datatypes are not supported with [bulk data transfer](static_bulk)

Another possibility to extend SOCI with custom data types is to use the `into_type<T<` and `use_type<T<` class templates, which specializations can be user-provided. These specializations need to implement the interface defined by, respectively, the `into_type_base` and `use_type_base`
classes.

Note that when specializing these template classes the only convention is that when the indicator
variable is used (see below), it should appear in the second position. Please refer to the library source code to see how this is done for the standard types.

#### <a name="object_relational"></a>  Object-relational mapping

SOCI provides a class called `values` specifically to enable object-relational mapping via `type_conversion` specializations.

For example, the following code maps a `Person` object to and from a database table containing columns `"ID"`, `"FIRST_NAME"`, `"LAST_NAME"`, and `"GENDER"`.

Note that the mapping is non-invasive - the `Person` object itself does not contain any SOCI-specific code:

    struct Person
    {
        int id;
        std::string firstName;
        std::string lastName;
        std::string gender;
    };

    namespace soci
    {
        template<<
        struct type_conversion<Person<
        {
            typedef values base_type;

            static void from_base(values const & v, indicator /* ind */, Person & p)
            {
                p.id = v.get<int<("ID");
                p.firstName = v.get<std::string<("FIRST_NAME");
                p.lastName = v.get<std::string<("LAST_NAME");

                // p.gender will be set to the default value "unknown"
                // when the column is null:
                p.gender = v.get<std::string<("GENDER", "unknown");

                // alternatively, the indicator can be tested directly:
                // if (v.indicator("GENDER") == i_null)
                // {
                //     p.gender = "unknown";
                // }
                // else
                // {
                //     p.gender = v.get<std::string<("GENDER");
                // }
            }

            static void to_base(const Person & p, values & v, indicator & ind)
            {
                v.set("ID", p.id);
                v.set("FIRST_NAME", p.firstName);
                v.set("LAST_NAME", p.lastName);
                v.set("GENDER", p.gender, p.gender.empty() ? i_null : i_ok);
                ind = i_ok;
            }
        };
    }

With the above `type_conversion` specialization in place, it is possible to use `Person` directly with SOCI:

    session sql(oracle, "service=db1 user=scott password=tiger");

    Person p;
    p.id = 1;
    p.lastName = "Smith";
    p.firstName = "Pat";
    sql << "insert into person(id, first_name, last_name) "
           "values(:ID, :FIRST_NAME, :LAST_NAME)", use(p);

    Person p1;
    sql << "select * from person", into(p1);
    assert(p1.id == 1);
    assert(p1.firstName + p.lastName == "PatSmith");
    assert(p1.gender == "unknown");

    p.firstName = "Patricia";
    sql << "update person set first_name = :FIRST_NAME "
           "where id = :ID", use(p);

##### Note:

The `values` class is currently not suited for use outside of `type_conversion`specializations. It is specially designed to facilitate object-relational mapping when used as shown above.


### <a name="blob"></a> Large objects (BLOBs)

The SOCI library provides also an interface for basic operations on large objects (BLOBs - Binary Large OBjects).

    blob b(sql); // sql is a session object
    sql << "select mp3 from mymusic where id = 123", into(b);

The following functions are provided in the `blob` interface, mimicking the file-like operations:

    *`std::size_t get_len();`
    *`std::size_t read(std::size_t offset, char *buf, std::size_t
    toRead);`
    *`std::size_t write(std::size_t offset, char const *buf,
    std::size_t toWrite);`
    *`std::size_t append(char const *buf, std::size_t toWrite);`
    *`void trim(std::size_t newLen);`

The `offset` parameter is always counted from the beginning of the BLOB's data.

##### Portability notes:

* The way to define BLOB table columns and create or destroy BLOB objects in the database varies between different database engines. Please see the SQL documentation relevant for the given server to learn how this is actually done. The test programs provided with the SOCI library can be also a simple source of full working examples.
* The `trim` function is not currently available for the PostgreSQL backend.