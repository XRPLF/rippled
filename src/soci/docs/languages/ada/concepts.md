# Ada Concepts

The SOCI-Ada library borrows its concepts and naming from the main SOCI project. They are shortly explained here in the bottom-up fashion.

One of the main properties of the library is that the data objects which are bound for transfer to and from the database server are managed by the library itself and are not directly visible from the user code. This ensures that no aliasing of objects occurs between Ada and underlying C++ code, which makes the inter-language interface easier and more resilient to the differences in how compilers handle the linkage. As a direct result of this design choice, users of SOCI-Ada need to instruct the library to internally create all objects that will be subject to data transfer.

There are two kinds of objects that can be managed by the SOCI-Ada library:

* *Into elements*, which are data objects that are transferred from the database to the user program as a result of executing a query. There are single into elements for binding single rows of results and vector into elements for binding whole bunches of data corresponding to whole result sets or their subranges. The into elements are identified by their *position*.

* *Use elements*, which are data objects that are transferred from the user program to the database as parameters of the query (and, if supported by the target database, that can be modified by the database server and transferred back to the user program). There are single use elements for binding parameters of single-row queries and vector use elements for binding whole bunches of data for transfer. The use elements are identified by their *name*.

The user program can read the current value of into and use elements and assign new values to use elements. Elements are strongly typed and the following types are currently supported:

* `String`
* `SOCI.DB_Integer`, which is defined by the library in terms of `Interfaces.C.int`
* `SOCI.DB_Long_Long_Integer`, which is defined in terms of `Interfaces.Integer_64`
* `SOCI.DB_Long_Float`, which is defined in terms of `Interfaces.C.double`
* `Ada.Calendar.Time`

Both into and use elements are managed for a single *statement*, which can be prepared once and executed once or many times, with data transfer handled during execution or fetch phase.

Statements can be managed explicitly, which is required if they are to be used repeteadly or when data transfer is needed or implicitly, which is a shorthand notation that is particularly useful with simple queries or DDL commands.

All statements are handled within the context of some *session*, which also supports *transactions*.

Sessions can be managed in isolation or as a group called *connection pool*, which helps to decouple tasking design choices from the concurrency policies at the database connection level. Sessions are *leased* from the pool for some time during which no other task can access them and returned back when no longer needed, where they can be acquired again by other tasks.

All potential problems are signalled via exceptions that have some descriptive message attached to them.
