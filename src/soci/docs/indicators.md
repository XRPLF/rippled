# Data Indicators

In order to support SQL NULL values and other conditions which are not real errors, the concept of *indicator* is provided.

## Select with NULL values

For example, when the following SQL query is executed:

```sql
select name from person where id = 7
```

there are three possible outcomes:

1. there is a person with id = 7 and her name is returned
2. there is a person with id = 7, but she has no name (her name is null in the database table)
3. there is no such person

Whereas the first alternative is easy to handle, the other two are more complex.
Moreover, they are not necessarily errors from the application's point of view and what's more interesting, they are *different* and the application may wish to detect which is the case.
The following example does this:

```cpp
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
```

The use of indicator variable is optional, but if it is not used and the result would be `i_null`,
then the exception is thrown.
This means that you should use indicator variables everywhere where the application logic (and database schema) allow the "attribute not set" condition.

## Insert with NULL values

Indicator variables can be also used when binding input data, to control whether the data is to be used as provided, or explicitly overrided to be null:

```cpp
int id = 7;
string name;
indicator ind = i_null;
sql << "insert into person(id, name) values(:id, :name)",
        use(id), use(name, ind);
```

In the above example, the row is inserted with `name` attribute set to null.

## Bulk operations with NULL values

Indicator variables can also be used in conjunction with vector based insert, update, and select statements:

```cpp
vector<string> names(100);
vector<indicator> inds;
sql << "select name from person where id = 7", into(names, inds);
```

The above example retrieves first 100 rows of data (or less).
The initial size of `names` vector provides the (maximum) number of rows that should be read.
Both vectors will be automatically resized according to the number of rows that were actually read.

The following example inserts null for each value of name:

```cpp
vector<int> ids;
vector<string> names;
vector<indicator> nameIndicators;

for (int i = 0; i != 10; ++i)
{
    ids.push_back(i);
    names.push_back("");
    nameIndicators.push_back(i_null);
}

sql << "insert into person(id, name) values(:id, :name)",
        use(ids), use(name, nameIndicators);
```

See also [Integration with Boost](boost.html) to learn how the Boost.Optional library can be used to handle null data conditions in a more natural way.
