## Connections and simple queries

### Connecting to the database

The `session` class encapsulates the database connection and other backend-related details, which are common to all the statements that will be later executed. It has a couple of overloaded constructors.

The most basic one expects two parameters: the requested backend factory object and the generic connection string,
which meaning is backend-dependent.

Example:

    session sql(oracle, "service=orcl user=scott password=tiger");

Another example might be:

    session sql(postgresql, "dbname=mydb");

Above, the `sql` object is a local (automatic) object that encapsulates the connection.

This `session` constructor either connects successfully, or throws an exception.

Another constructor allows to name backends at run-time and supports the dynamically loadable backends, which have to be compiled as shared libraries. The usage is similar to the above, but instead of providing the factory object, the backend name is expected:

    session sql("postgresql", "dbname=mydb");

For convenience, the URL-like form that combines both the backend name with connection parameters is supported as well:

    session sql("postgresql://dbname=mydb");

The last two constructors described above try to locate the shared library with the name `libsoci_ABC.so` (or `libsoci_ABC.dll` on Windows), where ABC is the backend name. In the above examples, the expected library name will be `libsoci_postgresql.so` for Unix-like systems.

The most general form of the constructor takes a single object of `connection_parameters` type which contains a pointer to the backend to use, the connection string and also any connection options. Using this constructor is the only way to pass any non-default options to the backend. For example, to suppress any interactive prompts when using ODBC backend you could do:

    connection_parameters parameters("odbc", "DSN=mydb");
    parameters.set_option(odbc_option_driver_complete, "0" /* SQL_DRIVER_NOPROMPT */);
    session sql(parameters);

Notice that you need to `#include &lt;soci-odbc.h&gt;` to obtain the option name declaration. The existing options are described in the backend-specific part of the documentation.


### Environment configuration

The `SOCI_BACKENDS_PATH` environment variable defines the set of paths where the shared libraries will be searched for. There can be many paths, separated by colons, and they are used from left to right until the library with the appropriate name is found. If this variable is not set or is empty, the current directory is used as a default path for dynamically loaded backends.



The run-time selection of backends is also supported with libraries linked statically. Each backend provides a separate function of the form `register_factory_*name*`, where `*name*` is a backend name. Thus:

    extern "C" void register_factory_postgresql();
    // ...
    register_factory_postgresql();
    session sql("postgresql://dbname=mydb");

The above example registers the backend for PostgreSQL and later creates the session object for that backend. This form is provided for those projects that prefer static linking but still wish to benefit from run-time backend selection.

An alternative way to set up the session is to create it in the disconnected state and connect later:

    session sql;

    // some time later:
    sql.open(postgresql, "dbname=mydb");

    // or:
    sql.open("postgresql://dbname=mydb");

    // or also:
    connection_parameters parameters("postgresql", "dbname=mydb");
    sql.open(parameters);

The rules for backend naming are the same as with the constructors described above.

The session can be also explicitly `close`d and `reconnect`ed, which can help with basic session error recovery. The `reconnect` function has no parameters and attempts to use the same values as those provided with earlier constructor or `open` calls.

See also the page devoted to [multithreading](multithreading.html) for a detailed description of connection pools.

It is possible to have many active `session`s at the same time, even using different backends.

### Portability note

The following backend factories are currently (as of 3.1.0 release) available:

* [mysql](backends/mysql.html) (requires `#include "soci-mysql.h"`)
* [oracle](backends/oracle.html) (requires `#include "soci-oracle.h"`)
* [postgresql](backends/postgresql.html) (requires `#include "soci-postgresql.h"`)

The following backends are also available, with various levels of completeness:

* [sqlite3](backends/sqlite3.html) (requires `#include "soci-sqlite3.h"`)
* [odbc](backends/odbc.html) (requires `#include "soci-odbc.h"`)
* [firebird](backends/firebird.html) (requires `#include "soci-firebird.h"`)