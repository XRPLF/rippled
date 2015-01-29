with SOCI;
with SOCI.PostgreSQL;
with Ada.Text_IO;
with Ada.Calendar;
with Ada.Exceptions;
with Ada.Numerics.Discrete_Random;
with Ada.Command_Line;

procedure PostgreSQL_Test is

   procedure Test_1 (Connection_String : in String) is
   begin
      Ada.Text_IO.Put_Line ("testing basic constructor function");

      declare
         S : SOCI.Session := SOCI.Make_Session (Connection_String);
      begin
         null;
      end;
   exception
      when E : SOCI.Database_Error =>
         Ada.Text_IO.Put_Line ("Database_Error: ");
         Ada.Text_IO.Put_Line (Ada.Exceptions.Exception_Message (E));
   end Test_1;

   procedure Test_2 (Connection_String : in String) is
      S : SOCI.Session;
   begin
      Ada.Text_IO.Put_Line ("testing open/close");

      S.Close;
      S.Open (Connection_String);
      S.Close;
   end Test_2;

   procedure Test_3 (Connection_String : in String) is
   begin
      Ada.Text_IO.Put_Line ("testing empty start/commit");

      declare
         S : SOCI.Session := SOCI.Make_Session (Connection_String);
      begin
         S.Start;
         S.Commit;
      end;
   end Test_3;

   procedure Test_4 (Connection_String : in String) is
   begin
      Ada.Text_IO.Put_Line ("testing simple statements");

      declare
         SQL : SOCI.Session := SOCI.Make_Session (Connection_String);
      begin
         SQL.Execute ("create table ada_test ( i integer )");
         SQL.Execute ("drop table ada_test");
      end;
   end Test_4;

   procedure Test_5 (Connection_String : in String) is
   begin
      Ada.Text_IO.Put_Line ("testing independent statements");

      declare
         SQL : SOCI.Session := SOCI.Make_Session (Connection_String);
         St_1 : SOCI.Statement := SOCI.Make_Statement (SQL);
         St_2 : SOCI.Statement := SOCI.Make_Statement (SQL);
      begin
         St_1.Prepare ("create table ada_test ( i integer )");
         St_2.Prepare ("drop table ada_test");
         St_1.Execute;
         St_2.Execute;
      end;
   end Test_5;

   procedure Test_6 (Connection_String : in String) is
   begin
      Ada.Text_IO.Put_Line ("testing data types and into elements");

      declare
         SQL : SOCI.Session := SOCI.Make_Session (Connection_String);
      begin
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos : SOCI.Into_Position;
         begin
            Pos := St.Into_String;
            St.Prepare ("select 'Hello'");
            St.Execute (True);
            pragma Assert (St.Get_Into_String (Pos) = "Hello");
         end;
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos : SOCI.Into_Position;
            Value : SOCI.DB_Integer;
            use type SOCI.DB_Integer;
         begin
            Pos := St.Into_Integer;
            St.Prepare ("select 123");
            St.Execute (True);
            Value := St.Get_Into_Integer (Pos);
            pragma Assert (Value = 123);
         end;
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos : SOCI.Into_Position;
            Value : SOCI.DB_Long_Long_Integer;
            use type SOCI.DB_Long_Long_Integer;
         begin
            Pos := St.Into_Long_Long_Integer;
            St.Prepare ("select 10000000000");
            St.Execute (True);
            Value := St.Get_Into_Long_Long_Integer (Pos);
            pragma Assert (Value = 10_000_000_000);
         end;
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos : SOCI.Into_Position;
            Value : SOCI.DB_Long_Float;
            use type SOCI.DB_Long_Float;
         begin
            Pos := St.Into_Long_Float;
            St.Prepare ("select 3.625");
            St.Execute (True);
            Value := St.Get_Into_Long_Float (Pos);
            pragma Assert (Value = SOCI.DB_Long_Float (3.625));
         end;
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos : SOCI.Into_Position;
            Value : Ada.Calendar.Time;
         begin
            Pos := St.Into_Time;
            St.Prepare ("select timestamp '2008-06-30 21:01:02'");
            St.Execute (True);
            Value := St.Get_Into_Time (Pos);
            pragma Assert (Ada.Calendar.Year (Value) = 2008);
            pragma Assert (Ada.Calendar.Month (Value) = 6);
            pragma Assert (Ada.Calendar.Day (Value) = 30);
            pragma Assert
              (Ada.Calendar.Seconds (Value) =
                 Ada.Calendar.Day_Duration (21 * 3_600 + 1 * 60 + 2));
         end;
      end;
   end Test_6;

   procedure Test_7 (Connection_String : in String) is
   begin
      Ada.Text_IO.Put_Line ("testing types with into vectors");

      declare
         SQL : SOCI.Session := SOCI.Make_Session (Connection_String);
         St : SOCI.Statement := SOCI.Make_Statement (SQL);
         Pos_Id : SOCI.Into_Position;
         Pos_Str : SOCI.Into_Position;
         Pos_LL : SOCI.Into_Position;
         Pos_LF : SOCI.Into_Position;
         Pos_TM : SOCI.Into_Position;

         use type SOCI.Data_State;
         use type Ada.Calendar.Time;
         use type SOCI.DB_Integer;
         use type SOCI.DB_Long_Long_Integer;
         use type SOCI.DB_Long_Float;

      begin
         SQL.Execute ("create table soci_test (" &
                        " id integer," &
                        " str varchar (20)," &
                        " ll bigint," &
                        " lf double precision," &
                        " tm timestamp" &
                        ")");
         SQL.Execute ("insert into soci_test (id, str, ll, lf, tm)" &
                        " values (1, 'abc', 10000000000, 3.0, timestamp '2008-06-30 21:01:02')");
         SQL.Execute ("insert into soci_test (id, str, ll, lf, tm)" &
                        " values (2, 'xyz', -10000000001, -3.125, timestamp '2008-07-01 21:01:03')");
         SQL.Execute ("insert into soci_test (id, str, ll, lf, tm)" &
                        " values (3, null, null, null, null)");

         Pos_Id := St.Into_Vector_Integer;
         Pos_Str := St.Into_Vector_String;
         Pos_LL := St.Into_Vector_Long_Long_Integer;
         Pos_LF := St.Into_Vector_Long_Float;
         Pos_TM := St.Into_Vector_Time;

         St.Into_Vectors_Resize (10); -- arbitrary batch size

         St.Prepare ("select id, str, ll, lf, tm from soci_test order by id");
         St.Execute (True);

         pragma Assert (St.Get_Into_Vectors_Size = 3);

         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 0) = 1);
         pragma Assert (St.Get_Into_Vector_State (Pos_Str, 0) = SOCI.Data_Not_Null);
         pragma Assert (St.Get_Into_Vector_String (Pos_Str, 0) = "abc");
         pragma Assert (St.Get_Into_Vector_Long_Long_Integer (Pos_LL, 0) = 10_000_000_000);
         pragma Assert (St.Get_Into_Vector_Long_Float (Pos_LF, 0) = SOCI.DB_Long_Float (3.0));
         pragma Assert (St.Get_Into_Vector_Time (Pos_TM, 0) =
                          Ada.Calendar.Time_Of (2008, 6, 30,
                                                Duration (21 * 3_600 + 1 * 60 + 2)));

         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 1) = 2);
         pragma Assert (St.Get_Into_Vector_State (Pos_Str, 1) = SOCI.Data_Not_Null);
         pragma Assert (St.Get_Into_Vector_String (Pos_Str, 1) = "xyz");
         pragma Assert (St.Get_Into_Vector_Long_Long_Integer (Pos_LL, 1) = -10_000_000_001);
         pragma Assert (St.Get_Into_Vector_Long_Float (Pos_LF, 1) = SOCI.DB_Long_Float (-3.125));
         pragma Assert (St.Get_Into_Vector_Time (Pos_TM, 1) =
                          Ada.Calendar.Time_Of (2008, 7, 1,
                                                Duration (21 * 3_600 + 1 * 60 + 3)));

         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 2) = 3);
         pragma Assert (St.Get_Into_Vector_State (Pos_Str, 2) = SOCI.Data_Null);
         pragma Assert (St.Get_Into_Vector_State (Pos_LL, 2) = SOCI.Data_Null);
         pragma Assert (St.Get_Into_Vector_State (Pos_LF, 2) = SOCI.Data_Null);
         pragma Assert (St.Get_Into_Vector_State (Pos_TM, 2) = SOCI.Data_Null);

         SQL.Execute ("drop table soci_test");
      end;
   end Test_7;

   procedure Test_8 (Connection_String : in String) is
   begin
      Ada.Text_IO.Put_Line ("testing multi-batch operation with into vectors");

      declare
         SQL : SOCI.Session := SOCI.Make_Session (Connection_String);
         St : SOCI.Statement := SOCI.Make_Statement (SQL);
         Pos_Id : SOCI.Into_Position;
         Got_Data : Boolean;

         use type SOCI.DB_Integer;
      begin
         SQL.Execute ("create table soci_test (" &
                        " id integer" &
                        ")");
         SQL.Execute ("insert into soci_test (id) values (1)");
         SQL.Execute ("insert into soci_test (id) values (2)");
         SQL.Execute ("insert into soci_test (id) values (3)");
         SQL.Execute ("insert into soci_test (id) values (4)");
         SQL.Execute ("insert into soci_test (id) values (5)");
         SQL.Execute ("insert into soci_test (id) values (6)");
         SQL.Execute ("insert into soci_test (id) values (7)");
         SQL.Execute ("insert into soci_test (id) values (8)");
         SQL.Execute ("insert into soci_test (id) values (9)");
         SQL.Execute ("insert into soci_test (id) values (10)");

         Pos_Id := St.Into_Vector_Integer;
         St.Into_Vectors_Resize (4); -- batch of 4 elements

         St.Prepare ("select id from soci_test order by id");
         St.Execute;

         Got_Data := St.Fetch;
         pragma Assert (Got_Data);
         pragma Assert (St.Get_Into_Vectors_Size = 4);
         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 0) = 1);
         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 1) = 2);
         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 2) = 3);
         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 3) = 4);

         Got_Data := St.Fetch;
         pragma Assert (Got_Data);
         pragma Assert (St.Get_Into_Vectors_Size = 4);
         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 0) = 5);
         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 1) = 6);
         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 2) = 7);
         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 3) = 8);

         Got_Data := St.Fetch;
         pragma Assert (Got_Data);
         pragma Assert (St.Get_Into_Vectors_Size = 2);
         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 0) = 9);
         pragma Assert (St.Get_Into_Vector_Integer (Pos_Id, 1) = 10);

         Got_Data := St.Fetch;
         pragma Assert (not Got_Data);
         pragma Assert (St.Get_Into_Vectors_Size = 0);

         SQL.Execute ("drop table soci_test");
      end;
   end Test_8;

   procedure Test_9 (Connection_String : in String) is
   begin
      Ada.Text_IO.Put_Line ("testing data types and use elements");

      declare
         SQL : SOCI.Session := SOCI.Make_Session (Connection_String);

         use type SOCI.DB_Integer;
         use type SOCI.DB_Long_Long_Integer;
         use type SOCI.DB_Long_Float;
      begin
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos : SOCI.Into_Position;
         begin
            St.Use_String ("value");
            St.Set_Use_String ("value", "123");
            Pos := St.Into_Integer;
            St.Prepare ("select cast(:value as integer)");
            St.Execute (True);
            pragma Assert (St.Get_Into_Integer (Pos) = 123);
         end;
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos : SOCI.Into_Position;
         begin
            St.Use_Integer ("value");
            St.Set_Use_Integer ("value", 123);
            Pos := St.Into_String;
            St.Prepare ("select cast(:value as text)");
            St.Execute (True);
            pragma Assert (St.Get_Into_String (Pos) = "123");
         end;
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos : SOCI.Into_Position;
         begin
            St.Use_Long_Long_Integer ("value");
            St.Set_Use_Long_Long_Integer ("value", 10_000_000_000);
            Pos := St.Into_String;
            St.Prepare ("select cast(:value as text)");
            St.Execute (True);
            pragma Assert (St.Get_Into_String (Pos) = "10000000000");
         end;
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos : SOCI.Into_Position;
         begin
            St.Use_Long_Float ("value");
            St.Set_Use_Long_Float ("value", SOCI.DB_Long_Float (5.625));
            Pos := St.Into_String;
            St.Prepare ("select cast(:value as text)");
            St.Execute (True);
            pragma Assert (St.Get_Into_String (Pos) = "5.625");
         end;
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos : SOCI.Into_Position;
         begin
            St.Use_Time ("value");
            St.Set_Use_Time ("value", Ada.Calendar.Time_Of
                               (2008, 7, 1, Ada.Calendar.Day_Duration (3723)));
            Pos := St.Into_String;
            St.Prepare ("select cast(:value as text)");
            St.Execute (True);
            pragma Assert (St.Get_Into_String (Pos) = "2008-07-01 01:02:03");
         end;
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos : SOCI.Into_Position;
            use type SOCI.Data_State;
         begin
            St.Use_Integer ("value");
            St.Set_Use_State ("value", SOCI.Data_Null);
            Pos := St.Into_Integer;
            St.Prepare ("select cast(:value as integer)");
            St.Execute (True);
            pragma Assert (St.Get_Into_State (Pos) = SOCI.Data_Null);
         end;
      end;
   end Test_9;

   procedure Test_10 (Connection_String : in String) is
   begin
      Ada.Text_IO.Put_Line ("testing vector use elements and row traversal with single into elements");

      declare
         SQL : SOCI.Session := SOCI.Make_Session (Connection_String);

         Time_1 : constant Ada.Calendar.Time := Ada.Calendar.Time_Of
           (2008, 7, 1, Ada.Calendar.Day_Duration (1));
         Time_2 : constant Ada.Calendar.Time := Ada.Calendar.Time_Of
           (2008, 7, 2, Ada.Calendar.Day_Duration (2));
         Time_3 : constant Ada.Calendar.Time := Ada.Calendar.Time_Of
           (2008, 7, 3, Ada.Calendar.Day_Duration (3));
         Time_4 : constant Ada.Calendar.Time := Ada.Calendar.Time_Of
           (2008, 7, 4, Ada.Calendar.Day_Duration (4));
         Time_5 : constant Ada.Calendar.Time := Ada.Calendar.Time_Of
           (2008, 7, 5, Ada.Calendar.Day_Duration (5));

      begin
         SQL.Execute ("create table soci_test (" &
                        " id integer," &
                        " str varchar (20)," &
                        " ll bigint," &
                        " lf double precision," &
                        " tm timestamp" &
                        ")");

         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
         begin
            St.Use_Vector_Integer ("id");
            St.Use_Vector_String ("str");
            St.Use_Vector_Long_Long_Integer ("ll");
            St.Use_Vector_Long_Float ("lf");
            St.Use_Vector_Time ("tm");

            St.Use_Vectors_Resize (6);
            St.Set_Use_Vector_Integer ("id", 0, 1);
            St.Set_Use_Vector_Integer ("id", 1, 2);
            St.Set_Use_Vector_Integer ("id", 2, 3);
            St.Set_Use_Vector_Integer ("id", 3, 4);
            St.Set_Use_Vector_Integer ("id", 4, 5);
            St.Set_Use_Vector_Integer ("id", 5, 6);
            St.Set_Use_Vector_String ("str", 0, "abc");
            St.Set_Use_Vector_String ("str", 1, "def");
            St.Set_Use_Vector_String ("str", 2, "ghi");
            St.Set_Use_Vector_String ("str", 3, "jklm");
            St.Set_Use_Vector_String ("str", 4, "no");
            St.Set_Use_Vector_State ("str", 5, SOCI.Data_Null);
            St.Set_Use_Vector_Long_Long_Integer ("ll", 0, 10_000_000_000);
            St.Set_Use_Vector_Long_Long_Integer ("ll", 1, 10_000_000_001);
            St.Set_Use_Vector_Long_Long_Integer ("ll", 2, 10_000_000_002);
            St.Set_Use_Vector_Long_Long_Integer ("ll", 3, 10_000_000_003);
            St.Set_Use_Vector_Long_Long_Integer ("ll", 4, 10_000_000_004);
            St.Set_Use_Vector_State ("ll", 5, SOCI.Data_Null);
            St.Set_Use_Vector_Long_Float ("lf", 0, SOCI.DB_Long_Float (0.0));
            St.Set_Use_Vector_Long_Float ("lf", 1, SOCI.DB_Long_Float (0.125));
            St.Set_Use_Vector_Long_Float ("lf", 2, SOCI.DB_Long_Float (0.25));
            St.Set_Use_Vector_Long_Float ("lf", 3, SOCI.DB_Long_Float (0.5));
            St.Set_Use_Vector_Long_Float ("lf", 4, SOCI.DB_Long_Float (0.625));
            St.Set_Use_Vector_State ("lf", 5, SOCI.Data_Null);
            St.Set_Use_Vector_Time ("tm", 0, Time_1);
            St.Set_Use_Vector_Time ("tm", 1, Time_2);
            St.Set_Use_Vector_Time ("tm", 2, Time_3);
            St.Set_Use_Vector_Time ("tm", 3, Time_4);
            St.Set_Use_Vector_Time ("tm", 4, Time_5);
            St.Set_Use_Vector_State ("tm", 5, SOCI.Data_Null);

            St.Prepare ("insert into soci_test (id, str, ll, lf, tm)" &
                          " values (:id, :str, :ll, :lf, :tm)");
            St.Execute (True);
         end;
         declare
            St : SOCI.Statement := SOCI.Make_Statement (SQL);
            Pos_Id : SOCI.Into_Position;
            Pos_Str : SOCI.Into_Position;
            Pos_LL : SOCI.Into_Position;
            Pos_LF : SOCI.Into_Position;
            Pos_TM : SOCI.Into_Position;
            Got_Data : Boolean;

            use type Ada.Calendar.Time;
            use type SOCI.Data_State;
            use type SOCI.DB_Integer;
            use type SOCI.DB_Long_Long_Integer;
            use type SOCI.DB_Long_Float;

         begin
            Pos_Id := St.Into_Integer;
            Pos_Str := St.Into_String;
            Pos_LL := St.Into_Long_Long_Integer;
            Pos_LF := St.Into_Long_Float;
            Pos_TM := St.Into_Time;

            St.Prepare ("select id, str, ll, lf, tm from soci_test order by id");
            St.Execute;

            Got_Data := St.Fetch;
            pragma Assert (Got_Data);
            pragma Assert (St.Get_Into_Integer (Pos_Id) = 1);
            pragma Assert (St.Get_Into_String (Pos_Str) = "abc");
            pragma Assert (St.Get_Into_Long_Long_Integer (Pos_LL) = 10_000_000_000);
            pragma Assert (St.Get_Into_Long_Float (Pos_LF) = SOCI.DB_Long_Float (0.0));
            pragma Assert (St.Get_Into_Time (Pos_TM) = Time_1);

            Got_Data := St.Fetch;
            pragma Assert (Got_Data);
            pragma Assert (St.Get_Into_Integer (Pos_Id) = 2);
            pragma Assert (St.Get_Into_String (Pos_Str) = "def");
            pragma Assert (St.Get_Into_Long_Long_Integer (Pos_LL) = 10_000_000_001);
            pragma Assert (St.Get_Into_Long_Float (Pos_LF) = SOCI.DB_Long_Float (0.125));
            pragma Assert (St.Get_Into_Time (Pos_TM) = Time_2);

            Got_Data := St.Fetch;
            pragma Assert (Got_Data);
            pragma Assert (St.Get_Into_Integer (Pos_Id) = 3);
            pragma Assert (St.Get_Into_String (Pos_Str) = "ghi");
            pragma Assert (St.Get_Into_Long_Long_Integer (Pos_LL) = 10_000_000_002);
            pragma Assert (St.Get_Into_Long_Float (Pos_LF) = SOCI.DB_Long_Float (0.25));
            pragma Assert (St.Get_Into_Time (Pos_TM) = Time_3);

            Got_Data := St.Fetch;
            pragma Assert (Got_Data);
            pragma Assert (St.Get_Into_Integer (Pos_Id) = 4);
            pragma Assert (St.Get_Into_String (Pos_Str) = "jklm");
            pragma Assert (St.Get_Into_Long_Long_Integer (Pos_LL) = 10_000_000_003);
            pragma Assert (St.Get_Into_Long_Float (Pos_LF) = SOCI.DB_Long_Float (0.5));
            pragma Assert (St.Get_Into_Time (Pos_TM) = Time_4);

            Got_Data := St.Fetch;
            pragma Assert (Got_Data);
            pragma Assert (St.Get_Into_Integer (Pos_Id) = 5);
            pragma Assert (St.Get_Into_String (Pos_Str) = "no");
            pragma Assert (St.Get_Into_Long_Long_Integer (Pos_LL) = 10_000_000_004);
            pragma Assert (St.Get_Into_Long_Float (Pos_LF) = SOCI.DB_Long_Float (0.625));
            pragma Assert (St.Get_Into_Time (Pos_TM) = Time_5);

            Got_Data := St.Fetch;
            pragma Assert (Got_Data);
            pragma Assert (St.Get_Into_State (Pos_Id) = SOCI.Data_Not_Null);
            pragma Assert (St.Get_Into_Integer (Pos_Id) = 6);
            pragma Assert (St.Get_Into_State (Pos_Str) = SOCI.Data_Null);
            pragma Assert (St.Get_Into_State (Pos_LL) = SOCI.Data_Null);
            pragma Assert (St.Get_Into_State (Pos_LF) = SOCI.Data_Null);
            pragma Assert (St.Get_Into_State (Pos_TM) = SOCI.Data_Null);

            Got_Data := St.Fetch;
            pragma Assert (not Got_Data);
         end;

         SQL.Execute ("drop table soci_test");
      end;
   end Test_10;

   procedure Test_11 (Connection_String : in String) is

      --  test parameters:
      Pool_Size : constant := 3;
      Number_Of_Tasks : constant := 10;
      Iterations_Per_Task : constant := 1000;

      type Small_Integer is mod 20;
      package My_Random is new Ada.Numerics.Discrete_Random (Small_Integer);
      Rand : My_Random.Generator;

      Pool : SOCI.Connection_Pool (Pool_Size);

   begin
      Ada.Text_IO.Put_Line ("testing connection pool");

      My_Random.Reset (Rand);

      for I in 1 .. Pool_Size loop
         Pool.Open (I, Connection_String);
      end loop;

      declare
         SQL : SOCI.Session := SOCI.Make_Session (Connection_String);
      begin
         SQL.Execute ("create table soci_test ( id integer )");
      end;

      declare

         task type Worker;
         task body Worker is
         begin

            for I in 1 .. Iterations_Per_Task loop
               declare
                  SQL : SOCI.Session;
                  V : Small_Integer;
               begin
                  Pool.Lease (SQL);

                  V := My_Random.Random (Rand);
                  SQL.Execute ("insert into soci_test (id) values (" &
                                 Small_Integer'Image (V) & ")");
               end;
            end loop;
         exception
            when others =>
               Ada.Text_IO.Put_Line ("An exception occured in the worker task.");
         end Worker;

         W : array (1 .. Number_Of_Tasks) of Worker;

      begin
         Ada.Text_IO.Put_Line ("--> waiting for the tasks to complete (might take a while)");
      end;

      declare
         SQL : SOCI.Session := SOCI.Make_Session (Connection_String);
      begin
         SQL.Execute ("drop table soci_test");
      end;
   end Test_11;

begin
   if Ada.Command_Line.Argument_Count /= 1 then
      Ada.Text_IO.Put_Line ("Expecting one argument: connection string");
      return;
   end if;

   declare
      Connection_String : String := Ada.Command_Line.Argument (1);
   begin
      Ada.Text_IO.Put_Line ("testing with " & Connection_String);

      SOCI.PostgreSQL.Register_Factory_PostgreSQL;

      Test_1 (Connection_String);
      Test_2 (Connection_String);
      Test_3 (Connection_String);
      Test_4 (Connection_String);
      Test_5 (Connection_String);
      Test_6 (Connection_String);
      Test_7 (Connection_String);
      Test_8 (Connection_String);
      Test_9 (Connection_String);
      Test_10 (Connection_String);
      Test_11 (Connection_String);
   end;
end PostgreSQL_Test;
