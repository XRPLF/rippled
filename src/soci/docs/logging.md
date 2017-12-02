# Logging

SOCI provides a very basic logging facility.

The following members of the `session` class support the basic logging functionality:

* `void set_log_stream(std::ostream * s);`
* `std::ostream * get_log_stream() const;`
* `std::string get_last_query() const;`

The first two functions allow to set the user-provided output stream object for logging.
The `NULL` value, which is the default, means that there is no logging.

An example use might be:

    session sql(oracle, "...");

    ofstream file("my_log.txt");
    sql.set_log_stream(&file);

    // ...

Each statement logs its query string before the preparation step (whether explicit or implicit) and therefore logging is effective whether the query succeeds or not.
Note that each prepared query is logged only once, independent on how many times it is executed.

The `get_last_query` function allows to retrieve the last used query.
