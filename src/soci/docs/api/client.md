# API Reference

The core client interface is a set of classes and free functions declared in the `soci.h` header file.
All names are dbeclared in the `soci` namespace.

There are also additional names declared in the `soci::details` namespace, but they are not supposed to be directly used by the users of the library and are therefore not documented here.
When such types are used in the declarations that are part of the "public" interface, they are replaced by "IT", which means "internal type".
Types related to the backend interface are named here.

## Commonly used types

The following types are commonly used in the rest of the interface:

```cpp
// data types, as seen by the user
enum data_type { dt_string, dt_date, dt_double, dt_integer, dt_long_long, dt_unsigned_long_long };

// the enum type for indicator variables
enum indicator { i_ok, i_null, i_truncated };

// the type used for reporting exceptions
class soci_error : public std::runtime_error { /* ... */ };
```

The `data_type` type defines the basic SOCI data types. User provided data types need to be associated with one of these basic types.

The `indicator` type defines the possible states of data.

The `soci_error` type is used for error reporting.

## class session

The `session` class encapsulates the connection to the database.

```cpp
class session
{
public:
    session();
    explicit session(connection_parameters const & parameters);
    session(backend_factory const & factory, std::string const & connectString);
    session(std::string const & backendName, std::string const & connectString);
    explicit session(std::string const & connectString);
    explicit session(connection_pool & pool);

    ~session();

    void open(backend_factory const & factory, std::string const & connectString);
    void open(std::string const & backendName, std::string const & connectString);
    void open(std::string const & connectString);
    void close();
    void reconnect();

    void begin();
    void commit();
    void rollback();

    *IT* once;
    *IT* prepare;

    template <typename T> *IT* operator<<(T const & t);

    bool got_data() const;

    bool get_next_sequence_value(std::string const & sequence, long & value);
    bool get_last_insert_id(std::string const & table, long & value);

    std::ostringstream & get_query_stream();

    void set_log_stream(std::ostream * s);
    std::ostream * get_log_stream() const;

    std::string get_last_query() const;

    void uppercase_column_names(bool forceToUpper);

    std::string get_dummy_from_table() const;
    std::string get_dummy_from_clause() const;

    details::session_backend * get_backend();

    std::string get_backend_name() const;
};
```

This class contains the following members:

* Various constructors. The default one creates the session in the disconnected state. The others expect the backend factory object, or the backend name, or the URL-like composed connection string or the special parameters object containing both the backend and the connection string as well as possibly other connection options. The last constructor creates a session proxy associated with the session that is available in the given pool and releases it back to the pool  when its lifetime ends. Example:

```cpp
session sql(postgresql, "dbname=mydb");
session sql("postgresql", "dbname=mydb");
session sql("postgresql://dbname=mydb");
```

* The constructors that take backend name as string load the shared library (if not yet loaded) with name computed as `libsoci_ABC.so` (or `libsoci_ABC.dll` on Windows) where `ABC` is the given backend name.
* `open`, `close` and `reconnect` functions for   reusing the same session object many times; the `reconnect` function attempts to establish the connection with the same parameters as most recently used with constructor or `open`. The arguments for `open` are treated in the same way as for constructors.
* `begin`, `commit` and `rollback` functions for transaction control.
* `once` member, which is used for performing *instant* queries that do not need to be separately prepared. Example:

```cpp
sql.once << "drop table persons";
```

* `prepare` member, which is used for statement preparation - the result of the statement preparation must be provided to the constructor of the `statement` class. Example:

```cpp
int i;
statement st = (sql.prepare <<
                "insert into numbers(value) values(:val)", use(i));
```

`operator<<` that is a shortcut forwarder to the equivalent operator of the `once` member. Example:

```cpp
sql << "drop table persons";
```

* `got_data` returns true if the last executed query had non-empty result.
* `get_next_sequence_value` returns true if the next value of   the sequence with the specified name was generated and returned in its second argument. Unless you can be sure that your program will use only   databases that support sequences, consider using this method in conjunction with `get_last_insert_id()` as explained in ["Working with sequences"](beyond.html#sequences) section.
* `get_last_insert_id` returns true if it could retrieve the last value automatically generated by the database for an auto-incremented field. Notice that although this method takes the table name, for some databases, such as Microsoft SQL Server and SQLite, this value is actually global, so you should attempt to retrieve it immediately after performing an insertion.
* `get_query_stream` provides direct access to the stream object that is used to accumulate the query text and exists in particular to allow the user to imbue specific locale to this stream.
* `set_log_stream` and `get_log_stream` functions for setting and getting the current stream object used for basic query logging. By default, it is `NULL`, which means no logging The string value that is actually logged into the stream is one-line verbatim copy of the query string provided by the user, without including any data from the `use` elements. The query is logged exactly once, before the preparation step.
* `get_last_query` retrieves the text of the last used query.
* `uppercase_column_names` allows to force all column names to uppercase in dynamic row description; this function is particularly useful for portability, since various database servers report column names differently (some preserve case, some change it).
* `get_dummy_from_table` and `get_dummy_from_clause()`: helpers for writing portable DML statements, see [DML helpers](statement.html#dml) for more details.
* `get_backend` returns the internal pointer to the concrete backend implementation of the session. This is provided for advanced users that need access to the functionality that is not otherwise available.
* `get_backend_name` is a convenience forwarder to the same function of the backend object.

See [Connections and simple queries](basics.html) for more examples.

## class connection_parameters

The `connection_parameters` class is a simple container for the backend pointer, connection string and any other connection options. It is used together with `session` constructor and `open()` method.

```cpp
class connection_parameters
{
public:
    connection_parameters();
    connection_parameters(backend_factory const & factory, std::string const & connectString);
    connection_parameters(std::string const & backendName, std::string const & connectString);
    explicit connection_parameters(std::string const & fullConnectString);

    void set_option(const char * name, std::string const & value);
    bool get_option(const char * name, std::string & value) const
};
```

The methods of this class are:

* Default constructor is rarely used as it creates an uninitialized object and the only way to initialize it later is to assign another, valid, connection_parameters object to this one.
* The other constructors correspond to the similar constructors of the `session` class and specify both the backend, either as a pointer to it or by name, and the connection string.
* `set_option` can be used to set the value of an option with the given name. Currently all option values are strings, so if you need to set a numeric option you need to convert it to a string first. If an option with the given name had been already set before, its old value is overwritten.

## class connection_pool

The `connection_pool` class encapsulates the thread-safe pool of connections and ensures that only one thread at a time has access to any connection that it manages.

```cpp
class connection_pool
{
public:
    explicit connection_pool(std::size_t size);
    ~connection_pool();

    session & at(std::size_t pos);

    std::size_t lease();
    bool try_lease(std::size_t & pos, int timeout);
    void give_back(std::size_t pos);
};
```

The operations of the pool are:

* Constructor that takes the intended size of the pool. After construction, the pool contains regular `session` objects in disconnected state.
* `at` function that provides direct access to any given entryin the pool. This function is *non-synchronized*.
* `lease` function waits until some entry is available (which means that it is not used) and returns the position of that entry in the pool, marking it as *locked*.
* `try_lease` acts like `lease`, but allows to set up a time-out (relative, in milliseconds) on waiting. Negative time-out value means no time-out. Returns `true` if the entry was obtained, in which case its position is written to the `pos` parametr, and `false` if no entry was available before the time-out.
* `give_back` should be called when the entry on the given position is no longer in use and can be passed to other requesting thread.

## class transaction

The class `transaction` can be used for associating the transaction with some code scope. It is a RAII wrapper for regular transaction operations that automatically rolls back in its destructor *if* the transaction was not explicitly committed before.

```cpp
class transaction
{
public:
    explicit transaction(session & sql);

    ~transaction();

    void commit();
    void rollback();

private:
    // ...
};
```

Note that objects of this class are not notified of other transaction related operations that might be executed by user code explicitly or hidden inside SQL queries. It is not recommended to mix different ways of managing transactions.

## function into

The function `into` is used for binding local output data (in other words, it defines where the results of the query are stored).

```cpp
template <typename T>
IT into(T & t);

template <typename T, typename T1>
IT into(T & t, T1 p1);

template <typename T>
IT into(T & t, indicator & ind);

template <typename T, typename T1>
IT into(T & t, indicator & ind, T1 p1);

template <typename T>
IT into(T & t, std::vector<indicator> & ind);
```

Example:

```cpp
int count;
sql << "select count(*) from person", into(count);
```

See [Binding local dat](exchange.html#bind_local) for more examples

## function use

The function `use` is used for binding local input data (in other words, it defines where the parameters of the query come from).

```cpp
template <typename T>
*IT* use(T & t);

template <typename T, typename T1>
*IT* use(T & t, T1 p1);

template <typename T>
*IT* use(T & t, indicator & ind);

template <typename T, typename T1>
*IT* use(T & t, indicator & ind, T1 p1);

template <typename T>
*IT* use(T & t, std::vector<indicator> const & ind);

template <typename T, typename T1>
*IT* use(T & t, std::vector<indicator> const & ind, T1 p1);
```

Example:

```cpp
int val = 7;
sql << "insert into numbers(val) values(:val)", use(val);
```

See [Binding local data](exchange.html#bind_local) for more examples.

## class statement

The `statement` class encapsulates the prepared statement.

```cpp
class statement
{
public:
    statement(session & s);
    statement(*IT* const & prep);
    ~statement();

    statement(statement const & other);
    void operator=(statement const & other);

    void alloc();
    void bind(values & v);
    void exchange(*IT* const & i);
    void exchange(*IT* const & u);
    void clean_up();
    void bind_clean_up();

    void prepare(std::string const & query);
    void define_and_bind();

    bool execute(bool withDataExchange = false);
    long long get_affected_rows();
    bool fetch();

    bool got_data() const;

    void describe();
    void set_row(row * r);
    void exchange_for_rowset(*IT* const & i);

    details::statement_backend * get_backend();
};
```

This class contains the following members:

* Constructor accepting the `session` object. This can be used for later query preparation. Example:

```cpp
statement stmt(sql);
```

* Constructor accepting the result of using `prepare` on the `session` object, see example provided above for the `session` class.
* Copy operations.
* `alloc` function, which allocates necessary internal resources.
* `bind` function, which is used to bind the `values` object - this is used in the object-relational mapping and normally called automatically.
* exchange functions for registering the binding of local data - they expect the result of calling the `into` or `use` functions and are normally invoked automatically.
* `clean_up` function for cleaning up resources, normally called automatically.
* `bind_clean_up` function for cleaning up any bound references. It allows to keep statement in cache and reuse it later with new references by calling `exchange` for each new bind variable.
* `prepare` function for preparing the statement for repeated execution.
* `define_and_bind` function for actually executing the registered bindings, normally called automatically.
* `execute` function for executing the statement. If its parameter is `false` then there is no data exchange with locally bound variables (this form should be used if later `fetch` of multiple rows is foreseen). Returns `true` if there was at least one row of data returned.
* `get_affected_rows` function returns the number of rows affected by the last statement. Returns `-1` if it's not implemented by the backend being used.
* `fetch` function for retrieving the next portion of the result. Returns `true` if there was new data.
* `got_data` return `true` if the most recent execution returned any rows.
* `describe` function for extracting the type information for the result (**Note:** no data is exchanged). This is normally called automatically and only when dynamic resultset binding is used.
* `set_row` function for associating the `statement` and `row` objects, normally called automatically.
* `exchange_for_rowset` as a special case for binding `rowset` objects.
* `get_backend` function that returns the internal pointer to the concrete backend implementation of the statement object. This is provided for advanced users that need access to the functionality that is not otherwise available.

See [Statement preparation and repeated execution](statements.html#preparation) for example uses.

Most of the functions from the `statement` class interface are called automatically, but can be also used explicitly. See [Interfaces](interfaces) for the description of various way to use this interface.

## class procedure

The `procedure` class encapsulates the call to the stored procedure and is aimed for higher portability of the client code.

```cpp
class procedure
{
public:
    procedure(*IT* const & prep);

    bool execute(bool withDataExchange = false);
    bool fetch();
    bool got_data() const;
};
```

The constructor expects the result of using `prepare` on the `session` object.

See [Stored procedures](statements.html#procedures) for examples.

## class type_conversion

The `type_conversion` class is a traits class that is supposed to be provided (specialized) by the user for defining conversions to and from one of the basic SOCI types.

```cpp
template <typename T>
struct type_conversion
{
    typedef T base_type;

    static void from_base(base_type const & in, indicator ind, T & out);

    static void to_base(T const & in, base_type & out, indicator & ind);
};
```

Users are supposed to properly implement the `from_base` and `to_base` functions in their specializations of this template class.

See [Extending SOCI to support custom (user-defined) C++ types](exchange.html#custom_types).

## class row

The `row` class encapsulates the data and type information retrieved for the single row when the dynamic rowset binding is used.

```cpp
class row
{
public:
    row();
    ~row();

    void uppercase_column_names(bool forceToUpper);

    std::size_t size() const;

    indicator get_indicator(std::size_t pos) const;
    indicator get_indicator(std::string const & name) const;

    column_properties const & get_properties (std::size_t pos) const;
    column_properties const & get_properties (std::string const & name) const;

    template <typename T>
    T get(std::size_t pos) const;

    template <typename T>
    T get(std::size_t pos, T const & nullValue) const;

    template <typename T>
    T get(std::string const & name) const;

    template <typename T>
    T get(std::string const & name, T const & nullValue) const;

    template <typename T>
    row const & operator>>(T & value) const;

    void skip(std::size_t num = 1) const;

    void reset_get_counter() const
};
```

This class contains the following members:

* Default constructor that allows to declare a `row` variable.
* `uppercase_column_names` - see the same function in the `session` class.
* `size` function that returns the number of columns in the row.
* `get_indicator` function that returns the indicator value for the given column (column is specified by position - starting from 0 - or by name).
* `get_properties` function that returns the properties of the column given by position (starting from 0) or by name.
* `get` functions that return the value of the column given by position or name. If the column contains null, then these functions either return the provided "default" `nullValue` or throw an exception.
* `operator>>` for convenience stream-like extraction interface. Subsequent calls to this function are equivalent to calling `get` with increasing position parameter, starting from the beginning.
* `skip` and `reset_get_counter` allow to change the order of data extraction for the above operator.

See [Dynamic resultset binding](exchange.html#dynamic) for examples.

## class column_properties

The `column_properties` class provides the type and name information about the particular column in a rowset.

```cpp
class column_properties
{
public:
    std::string get_name() const;
    data_type get_data_type() const;
};
```

This class contains the following members:

* `get_name` function that returns the name of the column.
* `get_data_type` that returns the type of the column.

See [Dynamic resultset binding](exchange.html#dynamic) for examples.

## class values

The `values` class encapsulates the data and type information and is used for object-relational mapping.

```cpp
class values
{
public:
    values();

    void uppercase_column_names(bool forceToUpper);

    indicator get_indicator(std::size_t pos) const;
    indicator get_indicator(std::string const & name) const;

    template <typename T>
    T get(std::size_t pos) const;

    template <typename T>
    T get(std::size_t pos, T const & nullValue) const;

    template <typename T>
    T get(std::string const & name) const;

    template <typename T>
    T get(std::string const & name, T const & nullValue) const;

    template <typename T>
    values const & operator>>(T & value) const;

    void skip(std::size_t num = 1) const;
    void reset_get_counter() const;

    template <typename T>
    void set(std::string const & name, T const & value, indicator indic = i_ok);

    template <typename T>
    void set(const T & value, indicator indic = i_ok);

    template <typename T>
    values & operator<<(T const & value);
};
```

This class contains the same members as the `row` class (with the same meaning) plus:

* `set` function for storing values in named columns or in subsequent positions.
* `operator<<` for convenience.

See [Object-relational mapping](exchange.html#object_relational) for examples.

## class blob

The `blob` class encapsulates the "large object" functionality.

```cpp
class blob
{
public:
    explicit blob(session & s);
    ~blob();

    std::size_t getLen();
    std::size_t read(std::size_t offset, char * buf, std::size_t toRead);
    std::size_t write(std::size_t offset, char const * buf, std::size_t toWrite);
    std::size_t append(char const * buf, std::size_t toWrite);
    void trim(std::size_t newLen);

    details::blob_backend * get_backend();
};
```

This class contains the following members:

* Constructor associating the `blob` object with the `session` object.
* `get_len` function that returns the size of the BLOB object.
* `read` function that reads the BLOB data into provided buffer.
* `write` function that writes the BLOB data from provided buffer.
* `append` function that appends to the existing BLOB data.
* `trim` function that truncates the existing data to the new length.
* `get_backend` function that returns the internal pointer to the concrete backend implementation of the BLOB object. This is provided for advanced users that need access to the functionality that is not otherwise available.

See [Large objects (BLOBs)](exchange.html#blob) for more discussion.

## class rowid

The `rowid` class encapsulates the "row identifier" object.

```cpp
class rowid
{
public:
    explicit rowid(Session & s);
    ~rowid();

    details::rowid_backend * get_backend();
};
```

This class contains the following members:

* Constructor associating the `rowid` object with the `session` object.
* `get_backend` function that returns the internal pointer to the concrete backend implementation of the `rowid` object.

## class backend_factory

The `backend_factory` class provides the abstract interface for concrete backend factories.

```cpp
struct backend_factory
{
    virtual details::session_backend * make_session(
        std::string const & connectString) const = 0;
};
```

The only member of this class is the `make_session` function that is supposed to create concrete backend implementation of the session object.

Objects of this type are declared by each backend and should be provided to the constructor of the `session` class. In simple programs users do not need to use this class directly, but the example use is:

```cpp
backend_factory & factory = postgresql;
std::string connectionParameters = "dbname=mydb";

session sql(factory, parameters);
```

## Simple Client Interface

The simple client interface is provided with other languages in mind, to allow easy integration of the SOCI library with script interpreters and those languages that have the ability to link directly with object files using the "C" calling convention.

The functionality of this interface is limited and in particular the dynamic rowset description and type conversions are not supported in this release. On the other hand, the important feature of this interface is that it does not require passing pointers to data managed by the user, because all data is handled at the SOCI side. This should make it easier to integrate SOCI with languages that have constrained ability to understand the C type system.

Users of this interface need to explicitly `#include <soci-simple.h>`.

```c
typedef void * session_handle;
session_handle soci_create_session(char const * connectionString);
void soci_destroy_session(session_handle s);

void soci_begin(session_handle s);
void soci_commit(session_handle s);
void soci_rollback(session_handle s);

int soci_session_state(session_handle s);
char const * soci_session_error_message(session_handle s);
```

The functions above provide the *session* abstraction with the help of opaque handle. The `soci_session_state` function returns `1` if there was no error during the most recently executed function and `0` otherwise, in which case the `soci_session_error_message` can be used to obtain a human-readable error description.

Note that the only function that cannot report all errors this way is `soci_create_session`, which returns `NULL` if it was not possible to create an internal object representing the session. However, if the proxy object was created, but the connection could not be established for whatever reason, the error message can be obtained in the regular way.

```c
typedef void *blob_handle;
blob_handle soci_create_blob(session_handle s);
void soci_destroy_blob(blob_handle b);

int soci_blob_get_len(blob_handle b);
int soci_blob_read(blob_handle b, int offset, char *buf, int toRead);
int soci_blob_write(blob_handle b, int offset, char const *buf, int toWrite);
int soci_blob_append(blob_handle b, char const *buf, int toWrite);
int soci_blob_trim(blob_handle b, int newLen);

int soci_blob_state(blob_handle b);
char const * soci_blob_error_message(blob_handle b);
```

The functions above provide the *blob* abstraction with the help of opaque handle. The `soci_blob_state` function returns `1` if there was no error during the most recently executed function and `0` otherwise, in which case the `soci_session_error_message` can be used to obtain a human-readable error description.

For easy error testing, functions `soci_blob_read`, `soci_blob_write`, `soci_blob_append`, and `soci_blob_trim` return `-1` in case of error and `soci_session_error_message` can be used to obtain a human-readable error description.

Note that the only function that cannot report all errors this way is `soci_create_blob`, which returns `NULL` if it was not possible to create an internal object representing the blob.

```c
typedef void * statement_handle;
statement_handle soci_create_statement(session_handle s);
void soci_destroy_statement(statement_handle st);

int soci_statement_state(statement_handle s);
char const * soci_statement_error_message(statement_handle s);
```

The functions above create and destroy the statement object. If the statement cannot be created by the `soci_create_statement` function, the error condition is set up in the related session object; for all other functions the error condition is set in the statement object itself.

```c
int soci_into_string   (statement_handle st);
int soci_into_int      (statement_handle st);
int soci_into_long_long(statement_handle st);
int soci_into_double   (statement_handle st);
int soci_into_date     (statement_handle st);
int soci_into_blob     (statement_handle st);

int soci_into_string_v   (statement_handle st);
int soci_into_int_v      (statement_handle st);
int soci_into_long_long_v(statement_handle st);
int soci_into_double_v   (statement_handle st);
int soci_into_date_v     (statement_handle st);
```

These functions create new data items for storing query results (*into elements*). These elements can be later identified by their position, which is counted from 0. For convenience, these function return the position of the currently added element. In case of error, `-1` is returned and the error condition is set in the statement object.

The `_v` versions create a `vector` into elements, which can be used
to retrieve whole arrays of results.

```c
int soci_get_into_state(statement_handle st, int position);
int soci_get_into_state_v(statement_handle st, int position, int index);
```

This function returns `1` if the into element at the given position has non-null value and `0` otherwise. The `_v` version works with `vector` elements and expects an array index.

```c
char const * soci_get_into_string   (statement_handle st, int position);
int          soci_get_into_int      (statement_handle st, int position);
long long    soci_get_into_long_long(statement_handle st, int position);
double       soci_get_into_double   (statement_handle st, int position);
char const * soci_get_into_date     (statement_handle st, int position);
blob_handle  soci_get_into_blob     (statement_handle st, int position);

char const * soci_get_into_string_v   (statement_handle st, int position, int index);
int          soci_get_into_int_v      (statement_handle st, int position, int index);
long long    soci_get_into_long_long_v(statement_handle st, int position, int index);
double       soci_get_into_double_v   (statement_handle st, int position, int index);
char const * soci_get_into_date_v     (statement_handle st, int position, int index);
```

The functions above allow to retrieve the current value of the given into element.

**Note:** The `date` function returns the date value in the "`YYYY MM DD HH mm ss`" string format.

```c
void soci_use_string   (statement_handle st, char const * name);
void soci_use_int      (statement_handle st, char const * name);
void soci_use_long_long(statement_handle st, char const * name);
void soci_use_double   (statement_handle st, char const * name);
void soci_use_date     (statement_handle st, char const * name);
void soci_use_blob     (statement_handle st, char const * name);

void soci_use_string_v   (statement_handle st, char const * name);
void soci_use_int_v      (statement_handle st, char const * name);
void soci_use_long_long_v(statement_handle st, char const * name);
void soci_use_double_v   (statement_handle st, char const * name);
void soci_use_date_v     (statement_handle st, char const * name);
```

The functions above allow to create new data elements that will be used to provide data to the query (*use elements*). The new elements can be later identified by given name, which must be unique for the given statement.

```c
void soci_set_use_state(statement_handle st, char const * name, int state);
```

The `soci_set_use_state` function allows to set the state of the given use element. If the `state` parameter is set to non-zero the use element is considered non-null (which is also the default state after creating the use element).

```c
int  soci_use_get_size_v(statement_handle st);
void soci_use_resize_v  (statement_handle st, int new_size);
```

These functions get and set the size of vector use elements (see comments for vector into elements above).

```c
void soci_set_use_string   (statement_handle st, char const * name, char const * val);
void soci_set_use_int      (statement_handle st, char const * name, int val);
void soci_set_use_long_long(statement_handle st, char const * name, long long val);
void soci_set_use_double   (statement_handle st, char const * name, double val);
void soci_set_use_date     (statement_handle st, char const * name, char const * val);
void soci_set_use_blob     (statement_handle st, char const * name, blob_handle blob);

void soci_set_use_state_v    (statement_handle st, char const * name, int index, int state);
void soci_set_use_string_v   (statement_handle st, char const * name, int index, char const * val);
void soci_set_use_int_v      (statement_handle st, char const * name, int index, int val);
void soci_set_use_long_long_v(statement_handle st, char const * name, int index, long long val);
void soci_set_use_double_v   (statement_handle st, char const * name, int index, double val);
void soci_set_use_date_v     (statement_handle st, char const * name, int index, char const * val);
```

The functions above set the value of the given use element, for both single and vector elements.

**Note:** The expected format for the data values is "`YYYY MM DD HH mm ss`".

```c
int          soci_get_use_state    (statement_handle st, char const * name);
char const * soci_get_use_string   (statement_handle st, char const * name);
int          soci_get_use_int      (statement_handle st, char const * name);
long long    soci_get_use_long_long(statement_handle st, char const * name);
double       soci_get_use_double   (statement_handle st, char const * name);
char const * soci_get_use_date     (statement_handle st, char const * name);
blob_handle  soci_get_use_blob     (statement_handle st, char const * name);
```

These functions allow to inspect the state and value of named use elements.

***Note:*** these functions are provide only for single use elements, not for vectors; the rationale for this is that modifiable use elements are not supported for bulk operations.

```c
void soci_prepare(statement_handle st, char const * query);
int  soci_execute(statement_handle st, int withDataExchange);
int  soci_fetch(statement_handle st);
int  soci_got_data(statement_handle st);
```

The functions above provide the core execution functionality for the statement object and their meaning is equivalent to the respective functions in the core C++ interface described above.
