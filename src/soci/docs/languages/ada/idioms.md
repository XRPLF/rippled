# Ada Idioms

As any other library, SOCI-Ada has its set of idioms that ensure optimal work in terms of performance and resource usage. Still, the optimal use will depend on the concrete usage scenario - the places where programmer choices are needed will be described explicitly.

The idioms below are provided as *complete programs* with the intent to make them more understandable and to give complete context of use for each idiom. The programs assume that the target database is PostgreSQL, but this can be changed by a different connection string in each place where the sessions are established. The programs use the Ada 2005 interface and some minor changes will be required to adapt them for Ada 95 compilers.

## Single query without data transfer

This type of query is useful for DDL commands and can be executed directly on the given session, without explicit statement management.

```ada
with SOCI;

procedure My_Program is

    SQL : SOCI.Session := SOCI.Make_Session ("postgresql://dbname=my_database");

begin

    SQL.Execute ("drop table some_table");

end My_Program;

```

Note: The session object is initialized by a constructor function call. An alternative would be to declare it without initialization and later use the `Open` operation to establish a physical connection with the database.

## Simple query without parameters resulting in one row of data

This type of query requires only single into elements, which together with the statement have to be manipulated explicitly.

```ada
with SOCI;
with Ada.Text_IO;

procedure My_Program is

    SQL : SOCI.Session := SOCI.Make_Session ("postgresql://dbname=my_database");
    St : SOCI.Statement := SOCI.Make_Statement (SQL);
    Pos : SOCI.Into_Position;

    Num_Of_Persons : SOCI.DB_Integer;

begin

    Pos := St.Into_Integer;
    St.Prepare ("select count(*) from persons");
    St.Execute (True);

    Num_Of_Persons := St.Get_Into_Integer (Pos);

    Ada.Text_IO.Put_Line ("Number of persons: " & SOCI.DB_Integer'Image (Num_Of_Persons));

end My_Program;
```

Note: The into element is inspected by providing the position value that was obtained at the time if was created. No operations are defined for the position type. There can be many into elements with a single query.

## Simple query with parameters and without results

This type of query requires only use elements.

```ada
with SOCI;

procedure My_Program is

    SQL : SOCI.Session := SOCI.Make_Session ("postgresql://dbname=my_database");
    St : SOCI.Statement := SOCI.Make_Statement (SQL);

begin

    St.Use_Integer ("increase");
    St.Set_Use_Integer ("increase", 1000);

    St.Prepare ("update persons set salary = salary + :increase");
    St.Execute (True);

end My_Program;
```

Note: The "`:increase`" in the query is a placeholder variable. There can be many such variables and each of them needs to be filled in by respective use element.

## Repeated query with parameters and without results

This type of query requires only use elements, but they can be set differently for each statement execution.

```ada
with SOCI;

procedure My_Program is

    SQL : SOCI.Session := SOCI.Make_Session ("postgresql://dbname=my_database");
    St : SOCI.Statement := SOCI.Make_Statement (SQL);

begin

    St.Use_String ("name");

    St.Prepare ("insert into countries(country_name) values(:name)");

    St.Set_Use_String ("name", "Poland");
    St.Execute (True);

    St.Set_Use_String ("name", "Switzerland");
    St.Execute (True);

    St.Set_Use_String ("name", "France");
    St.Execute (True);

end My_Program;
```

Note: Each time the query is executed, the *current* values of use elements are transferred to the database.

## Batch query with parameters and without results

This type of query requires vector use elements. Compare with the previous example.

```ada
with SOCI;

procedure My_Program is

    SQL : SOCI.Session := SOCI.Make_Session ("postgresql://dbname=my_database");
    St : SOCI.Statement := SOCI.Make_Statement (SQL);
    First : SOCI.Vector_Index;

    use type SOCI.Vector_Index;

begin

    St.Use_Vector_String ("name");

    St.Use_Vectors_Resize (3);

    First := St.Use_Vectors_First_Index;

    St.Set_Use_Vector_String ("name", First + 0, "Poland");
    St.Set_Use_Vector_String ("name", First + 1, "Switzerland");
    St.Set_Use_Vector_String ("name", First + 2, "France");

    St.Prepare ("insert into countries(country_name) values(:name)");
    St.Execute (True);

end My_Program;
```

Note:

The whole bunch of data is transferred to the database if the target database server supports it and the statement is automatically repeated otherwise. This is the preferred way to transfer many rows of data to the server when the data for all rows are known before executing the query.

Note:

The query can be executed many times and each time a new batch of data can be transferred to the server. The size of the batch (set by calling `Use_Vectors_Resize`) can be different each time the query is executed, but cannot be larger than the size that was used the first time. The size of the batch defines a tradeoff between the amount of data being transmitted in a single step (this influences the memory used by the user program and the time of a single call) and the number of executions required to handle big data sets. The optimal size of the batch will therefore differ depending on the application, but in general tens of thousands is a reasonable limit for a batch size - the performance of the whole operation is usually not affected above this value so there is no need to imply higher memory usage at the client side.

## Simple query with many rows of results

This type of query requires simple into elements.

```ada
with SOCI;
with Ada.Text_IO;

procedure My_Program is

    SQL : SOCI.Session := SOCI.Make_Session ("postgresql://dbname=my_database");
    St : SOCI.Statement := SOCI.Make_Statement (SQL);
    Pos : SOCI.Into_Position;

begin

    Pos := St.Into_String;

    St.Prepare ("select country_name from countries");
    St.Execute;

    while St.Fetch loop

        Ada.Text_IO.Put_Line (St.Get_Into_String (Pos));

    end loop;

end My_Program;
```

Note:

The loop above executes as many times as there are rows in the result. After each row is read, the into elements contain the respective values from that row. The `Execute` operation is called without parameter, which is `False` by default, meaning that no data transfer is intended. The data is being transferred only during the `Fetch` operation, which returns `False` when no data has been retrieved and the result is exhausted.

This type of query can have simple parameters which are fixed at the execution time.

## Batch query with many rows of results

This type of query requires vector into elements. Compare with previous example.

```ada
with SOCI;
with Ada.Text_IO;

procedure My_Program is

    SQL : SOCI.Session := SOCI.Make_Session ("postgresql://dbname=my_database");
    St : SOCI.Statement := SOCI.Make_Statement (SQL);
    Pos : SOCI.Into_Position;

    Batch_Size : constant := 10;

begin

    Pos := St.Into_Vector_String;
    St.Into_Vectors_Resize (Batch_Size);

    St.Prepare ("select country_name from countries");
    St.Execute;

    while St.Fetch loop

        for I in St.Into_Vectors_First_Index .. St.Into_Vectors_Last_Index loop

            Ada.Text_IO.Put_Line (St.Get_Into_Vector_String (Pos, I));

        end loop;

        St.Into_Vectors_Resize (Batch_Size);

    end loop;

end My_Program;
```

Note:

The loop above is nested. The outer `while` loop fetches consecutive batches of rows from the database with requested batch size; the returned batch can be smaller than requested (the into vector elements are downsized automatically if needed) and the intended batch size is requested again before repeating the `Fetch` operation. For each returned batch, the into vector elements are inspected in the inner `for` loop. This scheme ensures correct operation independently on the size of returned batch and is therefore a recommended idiom for efficiently returning many rows of data.

There is a tradeoff between efficiency and memory usage and this tradeoff is controlled by the requested batch size. Similarly to one of the examples above, there is no benefit from using batches bigger than tens of thousands of rows.

This type of query can have simple (not vectors) parameters that are fixed at execution time.

## Final note

Follow good database usage principles and avoid constructing queries by concatenating strings computed at run-time. Thanks to a good type system Ada is much better in preventing various SQL-injection attacks than weaker languages like PHP, but there is still a potential for vulnerability or at least performance loss. As a rule of thumb, rely on *use elements* to parameterize your queries and to provide clean separation between data and code. This will prevent many security vulnerabilities and will allow some servers to optimize their work by reusing already cached execution plans.
