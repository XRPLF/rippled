# Interfaces

One of the major features of SOCI, although not immediately visible, is the variety of interfaces (APIs) that are available for the user. These can be divided into *sugar*, *core* and *simple*.

## Sugar

The most exposed and promoted interface supports the syntax sugar that makes SOCI similar in look and feel to embedded SQL.
The example of application code using this interface is:

```cpp
session sql("postgresql://dbname=mydb");

int id = 123;
string name;

sql << "select name from persons where id = :id", into(name), use(id);
```

## Core

The above example is equivalent to the following, more explicit sequence of calls:

```cpp
session sql("postgresql://dbname=mydb");

int id = 123;
string name;

statement st(sql);
st.exchange(into(name));
st.exchange(use(id));
st.alloc();
st.prepare("select name from persons where id = :id");
st.define_and_bind();
st.execute(true);
```

The value of the *core* interface is that it is the basis for all other interfaces, and can be also used by developers to easily prepare their own convenience interfaces.
Users who cannot or for some reason do not want to use the natural *sugar* interface should try the *core* one as the foundation and access point to all SOCI functionality.

Note that the *sugar* interface wraps only those parts of the *core* that are related to data binding and query streaming.

## Simple

The *simple* interface is provided specifically to allow easy integration of the SOCI library with other languages that have the ability to link with binaries using the "C" calling convention.
To facilitate this integration, the *simple* interface does not use any pointers to data except C-style strings and opaque handles, but the consequence of this is that user data is managed by SOCI and not by user code.
To avoid exceptions passing the module boundaries, all errors are reported as state variables of relevant objects.

The above examples can be rewritten as (without error-handling):

```cpp
#include <soci-simple.h>

// ...
session_handle sql = soci_create_session("postgresql://dbname=mydb");

statement_handle st = soci_create_statement(sql);

soci_use_int(st, "id");
soci_set_use_int(st, "id", 123);

int namePosition = soci_into_string(st);

soci_prepare(st, "select name from persons where id = :id");

soci_execute(st, true);

char const * name = soci_get_into_string(st, namePosition);

printf("name is %s\n", name);

soci_destroy_statement(st);
soci_destroy_session(sql);
```

The *simple* interface supports single and bulk data exchange for static binding.
Dynamic row description is not supported in this release.

See [Simple client interface](/reference.html#simpleclient) reference documentation for more details.

## Low-level backend interface

The low-level backend interface allows to interact with backends directly and in principle allows to access the database without involving any other component.
There is no particular reason to use this interface in the user code.
