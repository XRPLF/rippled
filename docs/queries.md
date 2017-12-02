# Queries

## Simple SQL statements

In many cases, the SQL query is intended to be executed only once, which means that statement parsing and execution can go together. The `session` class provides a special `once` member, which triggers parsing and execution of such one-time statements:

```cpp
sql.once << "drop table persons";
```

For shorter syntax, the following form is also allowed:

```cpp
sql << "drop table persons";
```

The IOStream-like interface is exactly what it looks like, so that the statement text can be composed of many parts, involving anything that is *streamable* (including custom classes, if they have appropriate `operator<<`):

```cpp
string tableName = "persons";
sql << "drop table " << tableName;

int id = 123;
sql << "delete from companies where id = " << id;
```

## Query transformation

In SOCI 3.2.0, query transformation mechanism was introduced.

Query transformation is specified as user-defined unary function or callable function object with input parameter of type `std::string` which returns object of type `std::string` as well.

The query transformation function is registered for current database session using dedicated `session::set_query_transformation` method. Then, the transformation function is called with query string as argument just before the query is sent to database backend for execution or for preparation.

For one-time statements, query transformation is performed before each execution of statement. For prepared statements, query is transformed only once, before preparation, regardless how many times it is executed.

A few short examples how to use query transformation:

* defined as free function:

```cpp
std::string less_than_ten(std::string query)
{
    return query + " WHERE price < 10";
}

session sql(postgresql, "dbname=mydb");
sql.set_query_transformation(less_than_ten);
sql << "DELETE FROM item";
```

* defined as function object:

```cpp
struct order : std::unary_function<std::string, std::string&gt;
{
    order(std::string const&amp; by) : by_(by) {}

    result_type operator()(argument_type query) const
    {
        return query + " ORDER BY " + by_;
    }

    std::string by_;
};

char const* query = "SELECT * FROM product";
sql.set_query_transformation(order("price");
sql << query;
sql.set_query_transformation(order("id");
sql << query;
```

* defined as lambda function (since C++11):

```cpp
std::string dep = "sales";
sql.set_query_transformation(
    [&dep](std::string const&amp; query) {
        return query + " WHERE department = '" + dep + "'";
});
sql << "SELECT * FROM employee";
```

Query transformations enable users with simple mechanism to apply extra requirements to or interact with SQL statement being executed and that is without changing the SQL statement itself which may be passed from different
parts of application.

For example, the query transformation may be used to:

* modify or add clauses of SQL statements (i.e. `WHERE` clause with new condition)
* prefix table names with new schema to allow namespaces switch
* validate SQL statements
* perform sanitization checking for any unverified input
* apply database-specific features like add optimization hints to SQL statements (i.e. `SELECT /*+RULE*/ A FROM C` in Oracle 9)
