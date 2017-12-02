# Backends reference

This part of the documentation is provided for those who want towrite (and contribute!) their
own backends. It is anyway recommendedthat authors of new backend see the code of some existing
backend forhints on how things are really done.

The backend interface is a set of base classes that the actual backendsare supposed to specialize.
The main SOCI interface uses only theinterface and respecting the protocol (for example,
the order of function calls) described here.
Note that both the interface and theprotocol were initially designed with the Oracle database in
mind, which means that whereas it is quite natural with respect to the way Oracle API (OCI) works,
it might impose some implementation burden on otherbackends, where things are done differently
and therefore have to beadjusted, cached, converted, etc.

The interface to the common SOCI interface is defined in the `core/soci-backend.h` header file.
This file is dissected below.

All names are defined in either `soci` or `soci::details` namespace.

```cpp
// data types, as seen by the user
enum data_type
{
    dt_string, dt_date, dt_double, dt_integer, dt_long_long, dt_unsigned_long_long
};

// the enum type for indicator variables
enum indicator { i_ok, i_null, i_truncated };

// data types, as used to describe exchange format
enum exchange_type
{
    x_char,
    x_stdstring,
    x_short,
    x_integer,
    x_long_long,
    x_unsigned_long_long,
    x_double,
    x_stdtm,
    x_statement,
    x_rowid,
    x_blob
};

struct cstring_descriptor
{
    cstring_descriptor(char * str, std::size_t bufSize)
        : str_(str), bufSize_(bufSize) {}

    char * str_;
    std::size_t bufSize_;
};

// actually in error.h:
class soci_error : public std::runtime_error
{
public:
    soci_error(std::string const & msg);
};
```

The `data_type` enumeration type defines all types that form the core type support for SOCI.
The enum itself can be used by clients when dealing with dynamic rowset description.

The `indicator` enumeration type defines all recognized *states* of data.
The `i_truncated` state is provided for the case where the string is retrieved from the database
into the char buffer that is not long enough to hold the whole value.

The `exchange_type` enumeration type defines all possible types that can be used
with the `into` and `use` elements.

The `cstring_descriptor` is a helper class that allows to store the address of `char` buffer
together with its size.
The objects of this class are passed to the backend when the `x_cstring` type is involved.

The `soci_error` class is an exception type used for database-related (and also usage-related) errors.
The backends should throw exceptions of this or derived type only.

```cpp
class standard_into_type_backend
{
public:
    standard_into_type_backend() {}
    virtual ~standard_into_type_backend() {}

    virtual void define_by_pos(int& position, void* data, exchange_type type) = 0;

    virtual void pre_fetch() = 0;
    virtual void post_fetch(bool gotData, bool calledFromFetch, indicator* ind) = 0;

    virtual void clean_up() = 0;
};
```

The `standard_into_type_back_end` class implements the dynamic interactions with the simple
(non-bulk) `into` elements.
The objects of this class (or, rather, of the derived class implemented by the actual backend)
are created by the `statement` object when the `into` element is bound - in terms of lifetime
management, `statement` is the master of this class.

* `define_by_pos` - Called when the `into` element is bound, once and before the statement is executed. The `data` pointer points to the variable used for `into` element (or to the `cstring_descriptor` object, which is artificially created when the plain `char` buffer is used for data exchange). The `position` parameter is a "column number", assigned by the library. The backend should increase this parameter, according to the number of fields actually taken (usually 1).
* `pre_fetch` - Called before each row is fetched.
* `post_fetch` - Called after each row is fetched. The `gotData` parameter is `true` if the fetch operation really retrievedsome data and `false` otherwise; `calledFromFetch` is `true` when the call is from the fetch operation and `false` if it is from the execute operation (this is also the case for simple, one-time queries). In particular, `(calledFromFetch && !gotData)` indicates that there is an end-of-rowset condition. `ind` points to the indicator provided by the user, or is `NULL`, if there is no indicator.
* `clean_up` - Called once when the statement is destroyed.

The intended use of `pre_fetch` and `post_fetch` functions is to manage any internal buffer and/or data conversion foreach value retrieved from the database.
If the given server supportsbinary data transmission and the data format for the given type agreeswith what is used on the client machine, then these two functions neednot do anything; otherwise buffer management and data conversionsshould go there.

```cpp
class vector_into_type_backend
{
public:
    vector_into_type_backend() {}
    virtual ~vector_into_type_backend() {}

    virtual void define_by_pos(int& position, void* data, exchange_type type) = 0;

    virtual void pre_fetch() = 0;
    virtual void post_fetch(bool gotData, indicator* ind) = 0;

    virtual void resize(std::size_t sz) = 0;
    virtual std::size_t size() = 0;

    virtual void clean_up() = 0;
};
```

The `vector_into_type_back_end` has similar structure and purpose as the previous one, but is used for vectors (bulk data retrieval).

The `data` pointer points to the variable of type `std::vector<T>;` (and *not* to its internal buffer), `resize` is supposed to really resize the user-provided vector and `size` is supposed to return the current size of this vector.
The important difference with regard to the previous class is that `ind` points (if not `NULL`) to the beginning of the *array* of indicators.
The backend should fill this array according to the actual state of the retrieved data.

* `bind_by_pos` - Called for each `use` element, once and before the statement is executed - for those `use` elements that do not provide explicit names for parameter binding. The meaning of parameters is same as in previous classes.
* `bind_by_name` - Called for those `use` elements that provide the explicit name.
* `pre_use` - Called before the data is transmitted to the server (this means before the statement is executed, which can happen many times for the prepared statement). `ind` points to the indicator provided by the user (or is `NULL`).
* `post_use` - Called after statement execution. `gotData` and `ind` have the same meaning as in `standard_into_type_back_end::post_fetch`, and this can be used by those backends whose respective servers support two-way data exchange (like in/out parameters in stored procedures).

The intended use for `pre_use` and `post_use` methods is to manage any internal buffers and/or data conversion.
They can be called many times with the same statement.

```cpp
class vector_use_type_backend
{
public:
    virtual ~vector_use_type_backend() {}

    virtual void bind_by_pos(int& position,
        void* data, exchange_type type) = 0;
    virtual void bind_by_name(std::string const& name,
        void* data, exchange_type type) = 0;

    virtual void pre_use(indicator const* ind) = 0;

    virtual std::size_t size() = 0;

    virtual void clean_up() = 0;
};
```

Objects of this type (or rather of type derived from this one) are used to implement interactions with user-provided vector (bulk) `use` elements and are managed by the `statement` object.
The `data` pointer points to the whole vector object provided by the user (and *not* to its internal buffer); `ind` points to the beginning of the array of indicators (or is `NULL`).
The meaning of this interface is analogous to those presented above.

```cpp
class statement_backend
{
public:
    statement_backend() {}
    virtual ~statement_backend() {}

    virtual void alloc() = 0;
    virtual void clean_up() = 0;

    virtual void prepare(std::string const& query, statement_type eType) = 0;

    enum exec_fetch_result
    {
        ef_success,
        ef_no_data
    };

    virtual exec_fetch_result execute(int number) = 0;
    virtual exec_fetch_result fetch(int number) = 0;

    virtual long long get_affected_rows() = 0;
    virtual int get_number_of_rows() = 0;

    virtual std::string rewrite_for_procedure_call(std::string const& query) = 0;

    virtual int prepare_for_describe() = 0;
    virtual void describe_column(int colNum, data_type& dtype,
        std::string& column_name) = 0;

    virtual standard_into_type_backend* make_into_type_backend() = 0;
    virtual standard_use_type_backend* make_use_type_backend() = 0;
    virtual vector_into_type_backend* make_vector_into_type_backend() = 0;
    virtual vector_use_type_backend* make_vector_use_type_backend() = 0;
};
```

The `statement_backend` type implements the internals of the `statement` objects.
The objects of this class are created by the `session` object.

* `alloc` - Called once to allocate everything that is needed for the statement to work correctly.
* `clean_up` - Supposed to clean up the resources, called once.
* `prepare` - Called once with the text of the SQL query. For servers that support explicit query preparation, this is the place to do it.
* `execute` - Called to execute the query; if number is zero, the intent is not to exchange data with the user-provided objects (`into` and `use` elements); positive values mean the number of rows to exchange (more than 1 is used only for bulk operations).
* `fetch` - Called to fetch next bunch of rows; number is positive and determines the requested number of rows (more than 1 is used only for bulk operations).
* `get_affected_rows` - Called to determine the actual number of rows affected by data modifying statement.
* `get_number_of_rows` - Called to determine the actual number of rows retrieved by the previous call to `execute` or `fetch`.
* `rewrite_for_procedure_call` - Used when the `procedure` is used instead of `statement`, to call the stored procedure. This function should rewrite the SQL query (if necessary) to the form that will allow to execute the given procedure.
* `prepare_for_describe` - Called once when the `into` element is used with the `row` type, which means that dynamic rowset description should be performed. It is supposed to do whatever is needed to later describe the column properties and should return the number of columns.
* `describe_column` - Called once for each column (column numbers - `colNum` - start from 1), should fill its parameters according to the column properties.
* `make_into_type_backend`, `make_use_type_backend`, `make_vector_into_type_backend`, `make_vector_use_type_backend` - Called once for each `into` or `use` element, to create the objects of appropriate classes (described above).

**Notes:**

1. Whether the query is executed using the simple one-time syntax or is prepared, the `alloc`, `prepare` and `execute` functions are always called, in this order.
2. All `into` and `use` elements are bound (their `define_by_pos` or `bind_by_pos`/`bind_by_name` functions are called) *between* statement preparation and execution.

```cpp
class rowid_backend
{
public:
    virtual ~rowid_backend() {}
};
```

The `rowid_backend` class is a hook for the backends to provide their own state for the row identifier. It has no functions, since the only portable interaction with the row identifier object is to use it with `into` and `use` elements.

```cpp
class blob_backend
{
public:
    virtual ~blob_backend() {}

    virtual std::size_t get_len() = 0;
    virtual std::size_t read(std::size_t offset, char * buf,
        std::size_t toRead) = 0;
    virtual std::size_t write(std::size_t offset, char const * buf,
        std::size_t toWrite) = 0;
    virtual std::size_t append(char const * buf, std::size_t toWrite) = 0;
    virtual void trim(std::size_t newLen) = 0;
};
```

The `blob_backend` interface provides the entry points for the `blob` methods.

```cpp
class session_backend
{
public:
    virtual ~session_backend() {}

    virtual void begin() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;

    virtual bool get_next_sequence_value(session&, std::string const&, long&);
    virtual bool get_last_insert_id(session&, std::string const&, long&);

    virtual std::string get_backend_name() const = 0;

    virtual statement_backend * make_statement_backend() = 0;
    virtual rowid_backend * make_rowid_backend() = 0;
    virtual blob_backend * make_blob_backend() = 0;
};
```

The object of the class derived from `session_backend` implements the internals of the `session` object.

* `begin`, `commit`, `rollback` - Forward-called when the same functions of `session` are called by user.
* `get_next_sequence_value`, `get_last_insert_id` - Called to retrieve sequences or auto-generated values and every backend should define at least one of them to allow the code using auto-generated values to work.
* `make_statement_backend`, `make_rowid_backend`, `make_blob_backend` - Called to create respective implementations for the `statement`, `rowid` and `blob` classes.

```cpp
struct backend_factory
{
    virtual ~backend_factory() {}

    virtual details::session_backend * make_session(
        std::string const& connectString) const = 0;
};
```

The `backend_factory` is a base class for backend-provided factory class that is able to create valid sessions. The `connectString` parameter passed to `make_session` is provided here by the `session` constructor and contains only the backend-related parameters, without the backend name (if the dynamic backend loading is used).

The actual backend factory object is supposed to be provided by the backend implementation and declared in its header file. In addition to this, the `factory_ABC` function with the "C" calling convention and returning the pointer to concrete factory object should be provided, where `ABC` is the backend name.

The following example is taken from `soci-postgresql.h`, which declares entities of the PostgreSQL backend:

```cpp
struct postgresql_backend_factory : backend_factory
{
    virtual postgresql_session_backend* make_session(
        std::string const& connectString) const;
};
extern postgresql_backend_factory const postgresql;

extern "C"
{

// for dynamic backend loading
backend_factory const * factory_postgresql();

} // extern "C"
```

With the above declarations, it is enough to pass the `postgresql` factory name to the constructor of the `session` object, which will use this factory to create concrete implementations for any other objects that are needed, with the help of appropriate `make_XYZ` functions. Alternatively, the `factory_postgresql` function will be called automatically by the backend loader if the backend name is provided at run-time instead.

Note that the backend source code is placed in the `backends/*name*` directory (for example, `backends/oracle`) and the test driver is in `backends/*name*/test`. There is also `backends/empty` directory provided as a skeleton for development of new backends and their tests. It is recommended that all backends respect naming conventions by just appending their name to the base-class names. The backend name used for the global factory object should clearly identify the given database engine, like `oracle`, `postgresql`, `mysql`, and so on.
