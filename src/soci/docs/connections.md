# Connections

The `session` class encapsulates the database connection and other backend-related details, which are common to all the statements that will be later executed. It has a couple of overloaded constructors.

## Using backend factory

The most basic one expects two parameters: the requested backend factory object and the generic connection string,
which meaning is backend-dependent.

Example:

```cpp
session sql(oracle, "service=orcl user=scott password=tiger");
```

Another example might be:

```cpp
session sql(postgresql, "dbname=mydb");
```

Above, the `sql` object is a local (automatic) object that encapsulates the connection.

This `session` constructor either connects successfully, or throws an exception.

### Portability note

In case of SOCI linked against DLLs on Windows, the factory objects are not exported from the DLLs.
In order to avoid linker errors, access factory objects via dedicated backend functions
provided (eg. `factory_postgresql()`).

## Using loadable backends

Dynamically loadable backends are compiled as shared libraries and allow to select backends at run-time by name.

The usage is similar to the above, but instead of providing the factory object, the backend name is expected:

```cpp
session sql("postgresql", "dbname=mydb");
```

For convenience, the URL-like form that combines both the backend name with connection parameters is supported as well:

```cpp
session sql("postgresql://dbname=mydb");
```

The last two constructors described above try to locate the shared library with the name `libsoci_ABC.so` (or `libsoci_ABC.dll` on Windows), where ABC is the backend name.
In the above examples, the expected library name will be `libsoci_postgresql.so` for Unix-like systems.

The most general form of the constructor takes a single object of `connection_parameters` type which contains a pointer to the backend to use, the connection string and also any connection options.
Using this constructor is the only way to pass any non-default options to the backend.

For example, to suppress any interactive prompts when using ODBC backend you could do:

```cpp
connection_parameters parameters("odbc", "DSN=mydb");
parameters.set_option(odbc_option_driver_complete, "0" /* SQL_DRIVER_NOPROMPT */);
session sql(parameters);
```

Notice that you need to `#include<soci-odbc.h>` to obtain the option name declaration.
The existing options are described in the backend-specific part of the documentation.

IBM DB2 driver for ODBC and CLI also support the driver completion requests.
So, the DB2 backend provides similar option `db2_option_driver_complete` with `#include <soci-db1.h>` required to obtain the option name.

### Environment configuration

The `SOCI_BACKENDS_PATH` environment variable defines the set of paths where the shared libraries will be searched for.
There can be many paths, separated by colons, and they are used from left to right until the library with the appropriate name is found. If this variable is not set or is empty, the current directory is used as a default path for dynamically loaded backends.

## Using registered backends

The run-time selection of backends is also supported with libraries linked statically.

Each backend provides a separate function of the form `register_factory_*name*`, where `*name*` is a backend name. Thus:

```cpp
extern "C" void register_factory_postgresql();
// ...
register_factory_postgresql();
session sql("postgresql://dbname=mydb");
```

The above example registers the backend for PostgreSQL and later creates the session object for that backend.
This form is provided for those projects that prefer static linking but still wish to benefit from run-time backend selection.

An alternative way to set up the session is to create it in the disconnected state and connect later:

```cpp
session sql;

// some time later:
sql.open(postgresql, "dbname=mydb");

// or:
sql.open("postgresql://dbname=mydb");

// or also:
connection_parameters parameters("postgresql", "dbname=mydb");
sql.open(parameters);
```

The rules for backend naming are the same as with the constructors described above.

The session can be also explicitly `close`d and `reconnect`ed, which can help with basic session error recovery.
The `reconnect` function has no parameters and attempts to use the same values as those provided with earlier constructor or `open` calls.

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

## Connection failover

The `failover_callback` interface can be used as a callback channel for notifications of events that are automatically processed when the session is forcibly closed due to connectivity problems. The user can override the following methods:

```cpp
// Called when the failover operation has started,
// after discovering connectivity problems.
virtual void started();

// Called after successful failover and creating a new connection;
// the sql parameter denotes the new connection and allows the user
// to replay any initial sequence of commands (like session configuration).
virtual void finished(session & sql);

// Called when the attempt to reconnect failed,
// if the user code sets the retry parameter to true,
// then new connection will be attempted;
// the newTarget connection string is a hint that can be ignored
// by external means.
virtual void failed(bool & retry, std::string & newTarget);

// Called when there was a failure that prevents further failover attempts.
virtual void aborted();
```

The user-provided callback implementation can be installed (or reset) with:

```cpp
sql.set_failover_callback(myCallback);
```

### Portability note

The `failover_callback` functionality is currently supported only by PostgreSQL and Oracle backends (in the latter case the failover mechanism is governed by the Oracle-specific cluster configuration settings).
Other backends allow the callback object to be installed, but will ignore it and will not generate notification calls.
