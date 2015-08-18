## Errors

All DB-related errors manifest themselves as exceptions of type `soci_error`, which is derived from `std::runtime_error`.
This allows to handle database errors within the standard exception framework:

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

The only public method of `soci_error` is `std::string get_error_message() const`, which returns just the brief error message, without any additional information that can be present in the full error message returned by `what()`.


#### Portability note:

The Oracle backend can also throw the instances of the `oracle_soci_error`, which is publicly derived from `soci_error` and has an additional public `err_num_` member containing the Oracle error code:

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

#### Portability note:

The MySQL backend can throw instances of the `mysql_soci_error`, which is publicly derived from `soci_error` and has an additional public `err_num_` member containing the MySQL error code (as returned by `mysql_errno()`):

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

#### Portability note:

The PostgreSQL backend can also throw the instances of the `postgresql_soci_error`, which is publicly derived from `soci_error` and has an additional public `sqlstate()` member function returning the five-character "SQLSTATE" error code:

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