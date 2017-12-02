# Ada API Reference

The SOCI-Ada library is entirely implemented as a single package named `SOCI`. Additional child packages contain single procedures for static registration of backends - these child packages are not necessary for typical use, but can be useful to force static linking of backend code.

The following describes all publicly visible elements of this package:

```ada
--
--  General exception related to database and library usage.
--

Database_Error : exception;
```

Each problem related to the interaction with the database or to the incorrect usage of the library itself is signalled by raising this exception. Each occurrence of this exception has some human-readable error message that can be obtained by a call to `Ada.Exceptions.Exception_Message`.

```ada
--
--  Session.
--

type Session is tagged limited private;

not overriding
function Make_Session (Connection_String : in String) return Session;

not overriding
procedure Open (This : in out Session; Connection_String : in String);

not overriding
procedure Close (This : in out Session);

not overriding
function Is_Open (This : in Session) return Boolean;
```

The `Session` object can exist in two states: "connected" (or "open") and "disconnected". It can be created as connected at initialization time with a call to the constructor function `Make_Session` or left default-initialized in the disconnected state and later changed to connected with `Open` (the latter option is the only that is available in the Ada 95 version of the library). `Session` objects can be also associated with the connection pool, see below.

The `Connection_String` should have the form `"backendname://parameters"`, where `backendname` is used to construct the name of the dynamically loadable library that will be used to provide specific database services. Backends included in the current distribution of the main SOCI library are:

* `oracle` (implemented as `libsoci_oracle.so` or `libsoci_oracle.dll`)
* `postgresql` (implemented as `libsoci_postgresql.so` or `libsoci_postgresql.dll`)
* `mysql` (implemented as `libsoci_mysql.so` or `libsoci_mysql.dll`)

Other backends can be added to the library in the future or by the user himself, please see the documentation of the main SOCI library for details.

The `parameters` component of the `Connection_String` depends on the given backend, please see the documentation of the main SOCI project for the meaning and recognized options. The web pages related to the backends above are:

* [Oracle](http://soci.sourceforge.net/doc/backends/oracle.html)
* [PostgreSQL](http://soci.sourceforge.net/doc/backends/postgresql.html)
* [MySQL](http://soci.sourceforge.net/doc/backends/mysql.html)

The `Open` operation can be called only in the disconnected state (which changes the state of `Session` object to connected). The `Close` operation can be called in any state (provided that the session is not associated with the connection pool, see below) and after that the `Session` is in the disconnected state.

`Session` objects are closed automatically as part of their finalization. If the `Session` object is associated with the connection pool, the finalizer detaches from the pool without closing the connection.

```ada
--  Transaction management.

not overriding
procedure Start (This : in Session);

not overriding
procedure Commit (This : in Session);

not overriding
procedure Rollback (This : in Session);
```

These operations handle transactions. The exact meaning of transactions and whether transactions are automatic for some kinds of statements (and which ones) depend on the target database.

```ada
--  Immediate query execution.
not overriding
procedure Execute (This : in Session; Query : in String);
```

This operation allows to create implicit statement, prepare it for the given `Query` and execute it.

```ada
--
--  Connection pool management.
--

type Connection_Pool (Size : Positive) is tagged limited private;

not overriding
procedure Open
    (This : in out Connection_Pool;
    Position : in Positive;
    Connection_String : in String);

not overriding
procedure Close (This : in out Connection_Pool; Position : in Positive);

not overriding
procedure Lease (This : in out Connection_Pool; S : in out Session'Class);
```

The `Connection_Pool` encapsulates a fixed-size collection of sessions. Individual sessions are indexed from `1` to `Size` (provided as discriminant) and can be `Open`ed and `Close`d explicitly. Each connection in the pool can be created with different `Connection_String`, if needed.

The `Lease` operation allows to associate a given `Session` object (that has to be in the disconnected state itself) with one connection from the pool. The pool guarantees that at most one task can lease a given connection from the pool. If there are no free connections in the pool, the `Lease` operation will block waiting until some connection is freed.

The `Session` object that is associated with a connection from the pool automatically gives it back to pool as part of the `Session`'s finalizer. There is no other way to "detach" from the pool.

Note:

It is assumed that the lifetime of `Connection_Pool` encloses the lifetimes of all `Session` objects that are leased from it. There is no particular protection against it and it is possible to construct a code example with allocators that create partially overlapping `Connection_Pool` and `Session`, but this is considered obscure and not representative to the actual use scenarios. To avoid any potential problems, create `Connection_Pool` in the scope that encloses the scopes of leased `Session`s.

```ada
--
--  Statement.
--

type Statement (&lt;&gt;) is tagged limited private;

type Data_State is (Data_Null, Data_Not_Null);

type Into_Position is private;

type Vector_Index is new Natural;
```

The `Statement` type and supporting types. `Data_State` is used to indicate null values in the database sense - each value of into or use elements has a state from this type.

```ada
--  Statement preparation and execution.

not overriding
procedure Prepare (This : in Statement; Query : in String);

not overriding
procedure Execute
    (This : in Statement;
    With_Data_Exchange : in Boolean := False);

not overriding
function Execute
    (This : in Statement;
    With_Data_Exchange : in Boolean := False) return Boolean;

not overriding
function Fetch (This : in Statement) return Boolean;

not overriding
function Got_Data (This : in Statement) return Boolean;
```

The `Prepare` operation needs to be called before any other operation in the above group and it prepares the execution for the given `Query`. No into and use elements can be created after this operation is called.

The `Execute` operations cause the statement to execute, which might be combined with data exchange if requested. The function version of this operation returns `True` if some data has been returned back from the database server.

The `Fetch` function is used to transfer next portion of data (a single row or a whole bunch) from the database server and returns `True` if some data has been fetched. If this function returns `False` it means that no new data will be ever fetched for this statement and indicates the end-of-row condition.

The `Got_Data` function returns `True` if the last execution or fetch resulted in some data being transmitted from the database server.

```ada
--
--  Data items handling.
--

--  Database-specific types.
--  These types are most likely identical to standard Integer,
--  Long_Long_Integer and Long_Float, but are defined distinctly
--  to avoid interfacing problems with other compilers.

type DB_Integer is new Interfaces.C.int;
type DB_Long_Long_Integer is new Interfaces.Integer_64;
type DB_Long_Float is new Interfaces.C.double;
```

The data types used for interaction with the database are:

* `String`
* `DB_Integer`, defined above
* `DB_Long_Long_Integer`, defined above
* `DB_Long_Float`, defined above
* `Ada.Calendar.Time`

```ada
--  Creation of single into elements.

not overriding
function Into_String (This : in Statement) return Into_Position;

not overriding
function Into_Integer (This : in Statement) return Into_Position;

not overriding
function Into_Long_Long_Integer (This : in Statement) return Into_Position;

not overriding
function Into_Long_Float (This : in Statement) return Into_Position;

not overriding
function Into_Time (This : in Statement) return Into_Position;
```

These functions instruct the library to create internal simple into elements of the relevant type. They return the position of the into element, which can be later used to identify it.

Note: Simple into elements cannot be created together with vector into elements for the same statement.

Note: Simple into elements cannot be created together with vector into elements for the same statement.

```ada
--  Inspection of single into elements.

not overriding
function Get_Into_State
    (This : in Statement;
    Position : in Into_Position)
    return Data_State;

not overriding
function Get_Into_String
    (This : in Statement;
    Position : in Into_Position)
    return String;

not overriding
function Get_Into_Integer
    (This : in Statement;
    Position : in Into_Position)
    return DB_Integer;

not overriding
function Get_Into_Long_Long_Integer
    (This : in Statement;
    Position : in Into_Position)
    return DB_Long_Long_Integer;

not overriding
function Get_Into_Long_Float
    (This : in Statement;
    Position : in Into_Position)
    return DB_Long_Float;

not overriding
function Get_Into_Time
    (This : in Statement;
    Position : in Into_Position)
    return Ada.Calendar.Time;
```

These functions allow to inspect the state and value of the simple into element identified by its position. If the state of the given element is `Data_Null`, the data-reading functions raise exceptions for that element.

```ada
--  Inspection of vector into elements.

not overriding
function Get_Into_Vectors_Size (This : in Statement) return Natural;

not overriding
function Into_Vectors_First_Index (This : in Statement) return Vector_Index;

not overriding
function Into_Vectors_Last_Index (This : in Statement) return Vector_Index;

not overriding
procedure Into_Vectors_Resize (This : in Statement; New_Size : in Natural);
```

The `Get_Into_Vectors_Size` returns the number of entries in any of the vector into elements for the given statement.

The `Into_Vectors_First_Index` returns the lowest index value for vector into elements (which is always `0`, even if the vectors are empty). The `Into_Vectors_Last_Index` returns the last index of into vectors, and raises the `CONSTRAINT_ERROR` exception if the vectors are empty.

The `Into_Vectors_Resize` procedure allows to change the size of all use vectors for the given statement.

```ada
not overriding
function Get_Into_Vector_State
    (This : in Statement;
    Position : in Into_Position;
    Index : in Vector_Index)
    return Data_State;

not overriding
function Get_Into_Vector_String
    (This : in Statement;
    Position : in Into_Position;
    Index : in Vector_Index)
    return String;

not overriding
function Get_Into_Vector_Integer
    (This : in Statement;
    Position : in Into_Position;
    Index : in Vector_Index)
    return DB_Integer;

not overriding
function Get_Into_Vector_Long_Long_Integer
    (This : in Statement;
    Position : in Into_Position;
    Index : in Vector_Index)
    return DB_Long_Long_Integer;

not overriding
function Get_Into_Vector_Long_Float
    (This : in Statement;
    Position : in Into_Position;
    Index : in Vector_Index)
    return DB_Long_Float;

not overriding
function Get_Into_Vector_Time
    (This : in Statement;
    Position : in Into_Position;
    Index : in Vector_Index)
    return Ada.Calendar.Time;
```

These functions allow to inspect the state and value of the vector use element identified by its position and index. If the state of the given element is `Data_Null`, the data-reading functions raise exceptions for that element.

```ada
--  Creation of single use elements.

not overriding
procedure Use_String (This : in Statement; Name : in String);

not overriding
procedure Use_Integer (This : in Statement; Name : in String);

not overriding
procedure Use_Long_Long_Integer (This : in Statement; Name : in String);

not overriding
procedure Use_Long_Float (This : in Statement; Name : in String);

not overriding
procedure Use_Time (This : in Statement; Name : in String);
```

These functions instruct the library to create internal simple use elements of the relevant type, identified by the given `Name`.

Note:

* Simple use elements cannot be created together with vector use elements for the same statement.
* Vector use elements cannot be created together with any into elements for the same statement.

```ada
--  Creation of vector use elements.

not overriding
procedure Use_Vector_String (This : in Statement; Name : in String);

not overriding
procedure Use_Vector_Integer (This : in Statement; Name : in String);

not overriding
procedure Use_Vector_Long_Long_Integer (This : in Statement; Name : in String);

not overriding
procedure Use_Vector_Long_Float (This : in Statement; Name : in String);

not overriding
procedure Use_Vector_Time (This : in Statement; Name : in String);
```

These functions instruct the library to create internal vector use elements of the relevant type, identified by the given `Name`.

Note:

* Simple use elements cannot be created together with vector use elements for the same statement.
* Vector use elements cannot be created together with any into elements for the same statement.

```ada
--  Modifiers for single use elements.

not overriding
procedure Set_Use_State
    (This : in Statement;
    Name : in String;
    State : in Data_State);

not overriding
procedure Set_Use_String
    (This : in Statement;
    Name : in String;
    Value : in String);

not overriding
procedure Set_Use_Integer
    (This : in Statement;
    Name : in String;
    Value : in DB_Integer);

not overriding
procedure Set_Use_Long_Long_Integer
    (This : in Statement;
    Name : in String;
    Value : in DB_Long_Long_Integer);

not overriding
procedure Set_Use_Long_Float
    (This : in Statement;
    Name : in String;
    Value : in DB_Long_Float);

not overriding
procedure Set_Use_Time
    (This : in Statement;
    Name : in String;
    Value : in Ada.Calendar.Time);
```

These operations allow to modify the state and value of simple use elements. Setting the value of use element automatically sets its state to `Data_Not_Null`.

```ada
--  Modifiers for vector use elements.

not overriding
function Get_Use_Vectors_Size (This : in Statement) return Natural;

not overriding
function Use_Vectors_First_Index (This : in Statement) return Vector_Index;

not overriding
function Use_Vectors_Last_Index (This : in Statement) return Vector_Index;

not overriding
procedure Use_Vectors_Resize (This : in Statement; New_Size : in Natural);
```

The `Get_Use_Vectors_Size` returns the number of entries in any of the vector use elements for the given statement.

The `Use_Vectors_First_Index` returns the lowest index value for vector use elements (which is always `0`, even if the vectors are empty). The `Use_Vectors_Last_Index` returns the last index of use vectors, and raises the `CONSTRAINT_ERROR` exception if the vectors are empty.

The `Use_Vectors_Resize` procedure allows to change the size of all use vectors for the given statement.

```ada
not overriding
procedure Set_Use_Vector_State
    (This : in Statement;
    Name : in String;
    Index : in Vector_Index;
    State : in Data_State);

not overriding
procedure Set_Use_Vector_String
    (This : in Statement;
    Name : in String;
    Index : in Vector_Index;
    Value : in String);

not overriding
procedure Set_Use_Vector_Integer
    (This : in Statement;
    Name : in String;
    Index : in Vector_Index;
    Value : in DB_Integer);

not overriding
procedure Set_Use_Vector_Long_Long_Integer
    (This : in Statement;
    Name : in String;
    Index : in Vector_Index;
    Value : in DB_Long_Long_Integer);

not overriding
procedure Set_Use_Vector_Long_Float
    (This : in Statement;
    Name : in String;
    Index : in Vector_Index;
    Value : in DB_Long_Float);

not overriding
procedure Set_Use_Vector_Time
    (This : in Statement;
    Name : in String;
    Index : in Vector_Index;
    Value : in Ada.Calendar.Time);
```

These operations allow to modify the state and value of vector use elements. Setting the value of use element automatically sets its state to `Data_Not_Null`.

```ada
--  Inspection of single use elements.
--
--  Note: Use elements can be modified by the database if they
--        are bound to out and inout parameters of stored procedures
--        (although this is not supported by all database backends).
--        This feature is available only for single use elements.

not overriding
function Get_Use_State
    (This : in Statement;
    Name : in String)
    return Data_State;

not overriding
function Get_Use_String
    (This : in Statement;
    Name : in String)
    return String;

not overriding
function Get_Use_Integer
    (This : in Statement;
    Name : in String)
    return DB_Integer;

not overriding
function Get_Use_Long_Long_Integer
    (This : in Statement;
    Name : in String)
    return DB_Long_Long_Integer;

not overriding
function Get_Use_Long_Float
    (This : in Statement;
    Name : in String)
    return DB_Long_Float;

not overriding
function Get_Use_Time
    (This : in Statement;
    Name : in String)
    return Ada.Calendar.Time;
```

These functions allow to inspect the state and value of the simple use element identified by its name. If the state of the given element is `Data_Null`, the data-reading functions raise exceptions for that element.
