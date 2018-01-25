# Errors

All DB-related errors manifest themselves as exceptions of type `soci_error`, which is derived from `std::runtime_error`.
This allows to handle database errors within the standard exception framework:

```cpp
int main()
{
    try
    {
        // regular code
    }
    catch (std::exception const & e)
    {
        cerr << "Bang! " << e.what() << endl;
    }
}
```

The `soci_error` class exposes two public functions:

The `get_error_message() const` function returns `std::string` with a brief error message, without any additional information that can be present in the full error message returned by `what()`.

The `get_error_category() const` function returns one of the `error_category` enumeration values, which allows the user to portably react to some subset of common errors.
For example, `connection_error` or `constraint_violation` have meanings that are common across different database backends, even though the actual mechanics might differ.

## Portability

Error categories are not universally supported and there is no claim that all possible errors that are reported by the database server are covered or interpreted.
If the error category is not recognized by the backend, it defaults to `unknown`.

## MySQL

The MySQL backend can throw instances of the `mysql_soci_error`, which is publicly derived from `soci_error` and has an additional public `err_num_` member containing the MySQL error code (as returned by `mysql_errno()`):

```cpp
int main()
{
    try
    {
        // regular code
    }
    catch (soci::mysql_soci_error const & e)
    {
        cerr << "MySQL error: " << e.err_num_
            << " " << e.what() << endl;
    }
    catch (soci::exception const & e)
    {
        cerr << "Some other error: " << e.what() << endl;
    }
}
```

## Oracle

The Oracle backend can also throw the instances of the `oracle_soci_error`, which is publicly derived from `soci_error` and has an additional public `err_num_` member containing the Oracle error code:

```cpp
int main()
{
    try
    {
        // regular code
    }
    catch (soci::oracle_soci_error const & e)
    {
        cerr << "Oracle error: " << e.err_num_
            << " " << e.what() << endl;
    }
    catch (soci::exception const & e)
    {
        cerr << "Some other error: " << e.what() << endl;
    }
}
```

## PostgreSQL

The PostgreSQL backend can also throw the instances of the `postgresql_soci_error`, which is publicly derived from `soci_error` and has an additional public `sqlstate()` member function returning the five-character "SQLSTATE" error code:

```cpp
int main()
{
    try
    {
        // regular code
    }
    catch (soci::postgresql_soci_error const & e)
    {
        cerr << "PostgreSQL error: " << e.sqlstate()
            << " " << e.what() << endl;
    }
    catch (soci::exception const & e)
    {
        cerr << "Some other error: " << e.what() << endl;
    }
}
```
