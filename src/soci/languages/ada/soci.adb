--  Copyright (C) 2008-2011 Maciej Sobczak
--  Distributed under the Boost Software License, Version 1.0.
--  (See accompanying file LICENSE_1_0.txt or copy at
--  http://www.boost.org/LICENSE_1_0.txt)

with Ada.Strings.Fixed;
with Interfaces.C.Strings;

package body SOCI is

   procedure Check_Session_State (Handle : in Session_Handle) is

      function Soci_Session_State (S : in Session_Handle) return Interfaces.C.int;

      pragma Import (C, Soci_Session_State, "soci_session_state");

      function Soci_Session_Error_Message
        (S : in Session_Handle)
        return Interfaces.C.Strings.chars_ptr;
      pragma Import (C, Soci_Session_Error_Message, "soci_session_error_message");

      State : constant Interfaces.C.int := Soci_Session_State (Handle);
      Bad_State : constant Interfaces.C.int := 0;

      use type Interfaces.C.int;

   begin
      if State = Bad_State then
         declare
            Message : constant String :=
              Interfaces.C.Strings.Value (Soci_Session_Error_Message (Handle));
         begin
            raise Database_Error with Message;
         end;
      end if;
   end Check_Session_State;

   function Make_Session_Handle (Connection_String : in String) return Session_Handle is

      function Soci_Create_Session (C : in Interfaces.C.char_array) return Session_Handle;
      pragma Import (C, Soci_Create_Session, "soci_create_session");

      Connection_String_C : constant Interfaces.C.char_array :=
        Interfaces.C.To_C (Connection_String);

      Handle : constant Session_Handle :=
        Soci_Create_Session (Connection_String_C);

   begin
      if Handle = Null_Session_Handle then
         raise Database_Error with "Cannot create session object.";
      else
         return Handle;
      end if;
   end Make_Session_Handle;

   function Data_State_To_Int (State : in Data_State) return Interfaces.C.int is
   begin
      if State = Data_Not_Null then
         return 1;
      else
         return 0;
      end if;
   end Data_State_To_Int;

   function Int_To_Data_State (State : in Interfaces.C.int) return Data_State is
      use type Interfaces.C.int;
   begin
      if State /= 0 then
         return Data_Not_Null;
      else
         return Data_Null;
      end if;
   end Int_To_Data_State;

   procedure Check_Is_Open (This : in Session'Class) is
   begin
      if not This.Initialized then
         raise Database_Error with "Session is not initialized.";
      end if;
   end Check_Is_Open;

   procedure Check_Statement_State (Handle : in Statement_Handle) is

      function Soci_Statement_State (S : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Statement_State, "soci_statement_state");

      function Soci_Statement_Error_Message
        (S : in Statement_Handle)
        return Interfaces.C.Strings.chars_ptr;
      pragma Import (C, Soci_Statement_Error_Message, "soci_statement_error_message");

      State : constant Interfaces.C.int := Soci_Statement_State (Handle);
      Bad_State : constant Interfaces.C.int := 0;

      use type Interfaces.C.int;

   begin
      if State = Bad_State then
         declare
            Message : constant String :=
              Interfaces.C.Strings.Value (Soci_Statement_Error_Message (Handle));
         begin
            raise Database_Error with Message;
         end;
      end if;
   end Check_Statement_State;

   function String_To_Time (Source : in String) return Ada.Calendar.Time is

      Year_N : Natural;
      Month_N : Natural;
      Day_N : Natural;
      Hour_N : Natural;
      Minute_N : Natural;
      Second_N : Natural;

      procedure Get_Next_Number
        (Source : in String;
         Position : in out Natural;
         Result : out Natural) is

         I : Natural;
      begin
         I := Ada.Strings.Fixed.Index (Source => Source,
                                       Pattern => " ",
                                       From => Position);

         if I /= 0 then
            Result := Natural'Value (Source (Position .. I));
            Position := I + 1;
         else
            Result := Natural'Value (Source (Position .. Source'Last));
            Position := 0;
         end if;
      end Get_Next_Number;

      Pos : Natural := 1;

   begin

      Get_Next_Number (Source => Source, Position => Pos, Result => Year_N);
      Get_Next_Number (Source => Source, Position => Pos, Result => Month_N);
      Get_Next_Number (Source => Source, Position => Pos, Result => Day_N);
      Get_Next_Number (Source => Source, Position => Pos, Result => Hour_N);
      Get_Next_Number (Source => Source, Position => Pos, Result => Minute_N);
      Get_Next_Number (Source => Source, Position => Pos, Result => Second_N);

      return Ada.Calendar.Time_Of (Year_N, Month_N, Day_N,
                                   Duration (Hour_N * 3_600 + Minute_N * 60 + Second_N));

   end String_To_Time;

   function Time_To_String (Date : in Ada.Calendar.Time) return String is

      Year : Ada.Calendar.Year_Number;
      Month : Ada.Calendar.Month_Number;
      Day : Ada.Calendar.Day_Number;
      Seconds : Ada.Calendar.Day_Duration;

      Hour : Natural;
      Minute : Natural;
      Seconds_N : Natural;

   begin
      Ada.Calendar.Split (Date, Year, Month, Day, Seconds);
      Seconds_N := Natural (Seconds);

      Hour := Seconds_N / 3_600;
      Minute := (Seconds_N - Natural (Hour) * 3_600) / 60;
      Seconds_N := Seconds_N - Natural (Hour) * 3_600 - Natural (Minute) * 60;
      return Ada.Calendar.Year_Number'Image (Year) & " " &
        Ada.Calendar.Month_Number'Image (Month) & " " &
        Ada.Calendar.Day_Number'Image (Day) & " " &
        Natural'Image (Hour) & " " &
        Natural'Image (Minute) & " " &
        Natural'Image (Seconds_N);
   end Time_To_String;


   function Make_Session (Connection_String : in String) return Session is
   begin
      return S : Session do
         S.Handle := Make_Session_Handle (Connection_String);
         S.Initialized := True;
         Check_Session_State (S.Handle);
      end return;
   end Make_Session;

   procedure Open (This : in out Session; Connection_String : in String) is
   begin
      if This.Initialized then
         raise Database_Error with "Session is already initialized.";
      else
         declare
            Handle : constant Session_Handle :=
              Make_Session_Handle (Connection_String);
         begin
            Check_Session_State (Handle);

            This.Handle := Handle;
            This.Initialized := True;
         end;
      end if;
   end Open;

   procedure Close (This : in out Session) is

      procedure Soci_Destroy_Session (S : in Session_Handle);
      pragma Import (C, Soci_Destroy_Session, "soci_destroy_session");

   begin
      if This.Initialized then
         if This.Belongs_To_Pool then
            raise Database_Error with "Cannot close session - not an owner (session in pool).";
         else
            Soci_Destroy_Session (This.Handle);
            This.Initialized := False;
         end if;
      end if;
   end Close;

   function Is_Open (This : in Session) return Boolean is
   begin
      return This.Initialized;
   end Is_Open;

   procedure Finalize (This : in out Session) is
   begin
      if This.Initialized then
         if This.Belongs_To_Pool then
            This.Pool.all.Give_Back (This.Position_In_Pool);
            This.Initialized := False;
         else
            This.Close;
         end if;
      end if;
   end Finalize;

   procedure Start (This : in Session) is

      procedure Soci_Begin (S : in Session_Handle);
      pragma Import (C, Soci_Begin, "soci_begin");

   begin
      Check_Is_Open (This);
      Soci_Begin (This.Handle);
      Check_Session_State (This.Handle);
   end Start;

   procedure Commit (This : in Session) is

      procedure Soci_Commit (S : in Session_Handle);
      pragma Import (C, Soci_Commit, "soci_commit");

   begin
      Check_Is_Open (This);
      Soci_Commit (This.Handle);
      Check_Session_State (This.Handle);
   end Commit;

   procedure Rollback (This : in Session) is

      procedure Soci_Rollback (S : in Session_Handle);
      pragma Import (C, Soci_Rollback, "soci_rollback");

   begin
      Check_Is_Open (This);
      Soci_Rollback (This.Handle);
      Check_Session_State (This.Handle);
   end Rollback;

   procedure Execute (This : in Session; Query : in String) is
      S : Statement := Make_Statement (This);
   begin
      S.Prepare (Query);
      S.Execute;
   end Execute;

   protected body Connection_Pool_PS is

      procedure Open (Position : in Positive; Connection_String : in String) is
      begin
         if Position > Size then
            raise Database_Error with "Index out of range.";
         end if;

         Connections (Position).Open (Connection_String);
      end Open;

      procedure Close (Position : in Positive) is
      begin
         if Position > Size then
            raise Database_Error with "Index out of range.";
         end if;

         if Is_Used (Position) then
            raise Database_Error with "Cannot close connection that is currently in use.";
         end if;

         Connections (Position).Close;
      end Close;

      entry Lease (S : in out Session'Class) when Available is
         Found : Boolean := False;
      begin
         if S.Initialized then
            raise Database_Error with "This session is already initialized.";
         end if;

         --  Find some connection in the pool that is not currently used.
         for I in 1 .. Size loop
            if not Is_Used (I) then
               Check_Is_Open (Connections (I));

               S.Handle := Connections (I).Handle;
               S.Initialized := True;
               S.Belongs_To_Pool := True;
               S.Position_In_Pool := I;

               --  WORKAROUND:
               --  The S.Pool component is set in the Lease procedure
               --  of the Connection_Pool type, because here the access
               --  to the protected object could not be taken (compiler bug).

               Is_Used (I) := True;
               Found := True;
               exit;
            end if;
         end loop;

         if not Found then
            raise Database_Error with "Internal error.";
         end if;

         --  Update the Available flag.
         Found := False;
         for I in 1 .. Size loop
            if not Is_Used (I) then
               Found := True;
               exit;
            end if;
         end loop;
         Available := Found;

      end Lease;

      procedure Give_Back (Position : in Positive) is
      begin
         if Position > Size then
            raise Database_Error with "Index out of range.";
         end if;

         if not Is_Used (Position) then
            raise Database_Error with "Cannot give back connection that is not in use.";
         end if;

         Is_Used (Position) := False;
         Available := True;
      end Give_Back;

   end Connection_Pool_PS;

   procedure Open
     (This : in out Connection_Pool;
      Position : in Positive;
      Connection_String : in String) is
   begin
      This.Pool.Open (Position, Connection_String);
   end Open;

   procedure Close (This : in out Connection_Pool; Position : in Positive) is
   begin
      This.Pool.Close (Position);
   end Close;

   procedure Lease (This : in out Connection_Pool; S : in out Session'Class) is
   begin
      This.Pool.Lease (S);

      --  WORKAROUND:
      --  The S.Pool component is set here because the access
      --  to protected object cannot be taken in protected body (compiler bug.)

      --  JUSTIFICATION:
      --  The Unchecked_Access is taken here to enable the session to properly
      --  "unregister" from the pool in Session's Finalize.
      --  An alternative would be to rely on the user to explicitly unlock
      --  the appropriate entry in the pool, which is too error prone.
      --  It is assumed that connection pool always has wider lifetime
      --  than that of the session which is temporarily leased from the pool
      --  - this guarantees that S.Pool always points to a valid pool object.

      S.Pool := This.Pool'Unchecked_Access;
   end Lease;

   function Make_Statement (Sess : in Session'Class) return Statement is

      function Soci_Create_Statement (Sess : in Session_Handle) return Statement_Handle;
      pragma Import (C, Soci_Create_Statement, "soci_create_statement");

   begin
      Check_Is_Open (Sess);

      declare
         Handle : constant Statement_Handle :=
           Soci_Create_Statement (Sess.Handle);
      begin

         return S : Statement do
            S.Handle := Handle;
            S.Initialized := True;
            Check_Statement_State (S.Handle);
         end return;
      end;
   end Make_Statement;

   procedure Finalize (This : in out Statement) is

      procedure Soci_Destroy_Statement (S : in Statement_Handle);
      pragma Import (C, Soci_Destroy_Statement, "soci_destroy_statement");

   begin
      if This.Initialized then
         Soci_Destroy_Statement (This.Handle);
         This.Initialized := False;
      end if;
   end Finalize;

   procedure Prepare (This : in Statement; Query : in String) is

      procedure Soci_Prepare (St : in Statement_Handle; Q : in Interfaces.C.char_array);
      pragma Import (C, Soci_Prepare, "soci_prepare");

      Query_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Query);

   begin
      Soci_Prepare (This.Handle, Query_C);
      Check_Statement_State (This.Handle);
   end Prepare;

   procedure Execute (This : in Statement; With_Data_Exchange : in Boolean := False) is
      Result : constant Boolean := This.Execute (With_Data_Exchange);
   begin
      null;
   end Execute;

   function Execute
     (This : in Statement;
      With_Data_Exchange : in Boolean := False)
     return Boolean is

      function Soci_Execute
        (St : in Statement_Handle;
         WDE : in Interfaces.C.int)
        return Interfaces.C.int;
      pragma Import (C, Soci_Execute, "soci_execute");

      WDE_C : Interfaces.C.int;
      Result : Interfaces.C.int;

      use type Interfaces.C.int;

   begin
      if With_Data_Exchange then
         WDE_C := 1;
      else
         WDE_C := 0;
      end if;

      Result := Soci_Execute (This.Handle, WDE_C);
      Check_Statement_State (This.Handle);

      return Result /= 0;
   end Execute;

   function Fetch (This : in Statement) return Boolean is

      function Soci_Fetch (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Fetch, "soci_fetch");

      Result : constant Interfaces.C.int := Soci_Fetch (This.Handle);

      use type Interfaces.C.int;

   begin
      Check_Statement_State (This.Handle);
      return Result /= 0;
   end Fetch;

   function Got_Data (This : in Statement) return Boolean is

      function Soci_Got_Data (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Got_Data, "soci_got_data");

      Result : constant Interfaces.C.int := Soci_Got_Data (This.Handle);

      use type Interfaces.C.int;

   begin
      Check_Statement_State (This.Handle);
      return Result /= 0;
   end Got_Data;

   function Into_String (This : in Statement) return Into_Position is

      function Soci_Into_String (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Into_String, "soci_into_string");

      Result : constant Interfaces.C.int := Soci_Into_String (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Into_Position (Result);
   end Into_String;

   function Into_Integer (This : in Statement) return Into_Position is

      function Soci_Into_Int (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Into_Int, "soci_into_int");

      Result : constant Interfaces.C.int := Soci_Into_Int (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Into_Position (Result);
   end Into_Integer;

   function Into_Long_Long_Integer (This : in Statement) return Into_Position is

      function Soci_Into_Long_Long (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Into_Long_Long, "soci_into_long_long");

      Result : constant Interfaces.C.int := Soci_Into_Long_Long (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Into_Position (Result);
   end Into_Long_Long_Integer;

   function Into_Long_Float (This : in Statement) return Into_Position is

      function Soci_Into_Double (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Into_Double, "soci_into_double");

      Result : constant Interfaces.C.int := Soci_Into_Double (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Into_Position (Result);
   end Into_Long_Float;

   function Into_Time (This : in Statement) return Into_Position is

      function Soci_Into_Date (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Into_Date, "soci_into_date");

      Result : constant Interfaces.C.int := Soci_Into_Date (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Into_Position (Result);
   end Into_Time;

   function Into_Vector_String (This : in Statement) return Into_Position is

      function Soci_Into_String_V (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Into_String_V, "soci_into_string_v");

      Result : constant Interfaces.C.int := Soci_Into_String_V (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Into_Position (Result);
   end Into_Vector_String;

   function Into_Vector_Integer (This : in Statement) return Into_Position is

      function Soci_Into_Int_V (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Into_Int_V, "soci_into_int_v");

      Result : constant Interfaces.C.int := Soci_Into_Int_V (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Into_Position (Result);
   end Into_Vector_Integer;

   function Into_Vector_Long_Long_Integer (This : in Statement) return Into_Position is

      function Soci_Into_Long_Long_V (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Into_Long_Long_V, "soci_into_long_long_v");

      Result : constant Interfaces.C.int := Soci_Into_Long_Long_V (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Into_Position (Result);
   end Into_Vector_Long_Long_Integer;

   function Into_Vector_Long_Float (This : in Statement) return Into_Position is

      function Soci_Into_Double_V (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Into_Double_V, "soci_into_double_v");

      Result : constant Interfaces.C.int := Soci_Into_Double_V (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Into_Position (Result);
   end Into_Vector_Long_Float;

   function Into_Vector_Time (This : in Statement) return Into_Position is

      function Soci_Into_Date_V (St : in Statement_Handle) return Interfaces.C.int;
      pragma Import (C, Soci_Into_Date_V, "soci_into_date_v");

      Result : constant Interfaces.C.int := Soci_Into_Date_V (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Into_Position (Result);
   end Into_Vector_Time;

   function Get_Into_State
     (This : in Statement;
      Position : in Into_Position)
     return Data_State is

      function Soci_Get_Into_State
        (St : in Statement_Handle;
         P : in Interfaces.C.int)
        return Interfaces.C.int;
      pragma Import (C, Soci_Get_Into_State, "soci_get_into_state");

      Result : constant Interfaces.C.int :=
        Soci_Get_Into_State (This.Handle, Interfaces.C.int (Position));

      use type Interfaces.C.int;

   begin
      Check_Statement_State (This.Handle);
      return Int_To_Data_State (Result);
   end Get_Into_State;

   function Get_Into_String
     (This : in Statement;
      Position : in Into_Position)
     return String is

      function Soci_Get_Into_String
        (St : in Statement_Handle;
         P : in Interfaces.C.int)
        return Interfaces.C.Strings.chars_ptr;
      pragma Import (C, Soci_Get_Into_String, "soci_get_into_string");

      Result : constant Interfaces.C.Strings.chars_ptr :=
        Soci_Get_Into_String (This.Handle, Interfaces.C.int (Position));

   begin
      Check_Statement_State (This.Handle);
      return Interfaces.C.Strings.Value (Result);
   end Get_Into_String;

   function Get_Into_Integer
     (This : in Statement;
      Position : in Into_Position)
     return DB_Integer is

      function Soci_Get_Into_Int
        (St : in Statement_Handle;
         P : in Interfaces.C.int)
        return Interfaces.C.int;
      pragma Import (C, Soci_Get_Into_Int, "soci_get_into_int");

      Result : constant Interfaces.C.int :=
        Soci_Get_Into_Int (This.Handle, Interfaces.C.int (Position));

   begin
      Check_Statement_State (This.Handle);
      return DB_Integer (Result);
   end Get_Into_Integer;

   function Get_Into_Long_Long_Integer
     (This : in Statement;
      Position : in Into_Position)
     return DB_Long_Long_Integer is

      function Soci_Get_Into_Long_Long
        (St : in Statement_Handle;
         P : in Interfaces.C.int)
        return Interfaces.Integer_64;
      pragma Import (C, Soci_Get_Into_Long_Long, "soci_get_into_long_long");

      Result : constant Interfaces.Integer_64 :=
        Soci_Get_Into_Long_Long (This.Handle, Interfaces.C.int (Position));

   begin
      Check_Statement_State (This.Handle);
      return DB_Long_Long_Integer (Result);
   end Get_Into_Long_Long_Integer;

   function Get_Into_Long_Float
     (This : in Statement;
      Position : in Into_Position)
     return DB_Long_Float is

      function Soci_Get_Into_Double
        (St : in Statement_Handle;
         P : in Interfaces.C.int)
        return Interfaces.C.double;
      pragma Import (C, Soci_Get_Into_Double, "soci_get_into_double");

      Result : constant Interfaces.C.double :=
        Soci_Get_Into_Double (This.Handle, Interfaces.C.int (Position));

   begin
      Check_Statement_State (This.Handle);
      return DB_Long_Float (Result);
   end Get_Into_Long_Float;

   function Get_Into_Time
     (This : in Statement;
      Position : in Into_Position)
     return Ada.Calendar.Time is

      function Soci_Get_Into_Date
        (St : in Statement_Handle;
         P : in Interfaces.C.int)
        return Interfaces.C.Strings.chars_ptr;
      pragma Import (C, Soci_Get_Into_Date, "soci_get_into_date");

      Result_C : constant Interfaces.C.Strings.chars_ptr :=
        Soci_Get_Into_Date (This.Handle, Interfaces.C.int (Position));
      Result : constant String := Interfaces.C.Strings.Value (Result_C);

   begin
      Check_Statement_State (This.Handle);
      return String_To_Time (Result);
   end Get_Into_Time;

   function Get_Into_Vectors_Size (This : in Statement) return Natural is

      function Soci_Into_Get_Size_V
        (St : in Statement_Handle)
        return Interfaces.C.int;
      pragma Import (C, Soci_Into_Get_Size_V, "soci_into_get_size_v");

      Result_C : constant Interfaces.C.int := Soci_Into_Get_Size_V (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Natural (Result_C);
   end Get_Into_Vectors_Size;

   function Into_Vectors_First_Index (This : in Statement) return Vector_Index is
   begin
      return 0;
   end Into_Vectors_First_Index;

   function Into_Vectors_Last_Index (This : in Statement) return Vector_Index is
   begin
      return Vector_Index (This.Get_Into_Vectors_Size - 1);
   end Into_Vectors_Last_Index;

   procedure Into_Vectors_Resize (This : in Statement; New_Size : in Natural) is

      procedure Soci_Into_Resize_V
        (St : in Statement_Handle;
         New_Size : in Interfaces.C.int);
      pragma Import (C, Soci_Into_Resize_V, "soci_into_resize_v");

   begin
      Soci_Into_Resize_V (This.Handle, Interfaces.C.int (New_Size));
      Check_Statement_State (This.Handle);
   end Into_Vectors_Resize;

   function Get_Into_Vector_State
     (This : in Statement;
      Position : in Into_Position;
      Index : in Vector_Index)
     return Data_State is

      function Soci_Get_Into_State_V
        (St : in Statement_Handle;
         P : in Interfaces.C.int;
         I : in Interfaces.C.int)
        return Interfaces.C.int;
      pragma Import (C, Soci_Get_Into_State_V, "soci_get_into_state_v");

      Result : constant Interfaces.C.int :=
        Soci_Get_Into_State_V
        (This.Handle,
         Interfaces.C.int (Position),
         Interfaces.C.int (Index));

      use type Interfaces.C.int;

   begin
      Check_Statement_State (This.Handle);
      return Int_To_Data_State (Result);
   end Get_Into_Vector_State;

   function Get_Into_Vector_String
     (This : in Statement;
      Position : in Into_Position;
      Index : in Vector_Index) return String is

      function Soci_Get_Into_String_V
        (St : in Statement_Handle;
         P : in Interfaces.C.int;
         I : in Interfaces.C.int)
        return Interfaces.C.Strings.chars_ptr;
      pragma Import (C, Soci_Get_Into_String_V, "soci_get_into_string_v");

      Result : constant Interfaces.C.Strings.chars_ptr :=
        Soci_Get_Into_String_V (This.Handle,
                                Interfaces.C.int (Position),
                                Interfaces.C.int (Index));

   begin
      Check_Statement_State (This.Handle);
      return Interfaces.C.Strings.Value (Result);
   end Get_Into_Vector_String;

   function Get_Into_Vector_Integer
     (This : in Statement;
      Position : in Into_Position;
      Index : in Vector_Index)
     return DB_Integer is

      function Soci_Get_Into_Int_V
        (St : in Statement_Handle;
         P : in Interfaces.C.int;
         I : in Interfaces.C.int)
        return Interfaces.C.int;
      pragma Import (C, Soci_Get_Into_Int_V, "soci_get_into_int_v");

      Result : constant Interfaces.C.int :=
        Soci_Get_Into_Int_V (This.Handle,
                             Interfaces.C.int (Position),
                             Interfaces.C.int (Index));

   begin
      Check_Statement_State (This.Handle);
      return DB_Integer (Result);
   end Get_Into_Vector_Integer;

   function Get_Into_Vector_Long_Long_Integer
     (This : in Statement;
      Position : in Into_Position;
      Index : in Vector_Index)
     return DB_Long_Long_Integer is

      function Soci_Get_Into_Long_Long_V
        (St : in Statement_Handle;
         P : in Interfaces.C.int;
         I : in Interfaces.C.int)
        return Interfaces.Integer_64;
      pragma Import (C, Soci_Get_Into_Long_Long_V, "soci_get_into_long_long_v");

      Result : constant Interfaces.Integer_64 :=
        Soci_Get_Into_Long_Long_V (This.Handle,
                                   Interfaces.C.int (Position),
                                   Interfaces.C.int (Index));

   begin
      Check_Statement_State (This.Handle);
      return DB_Long_Long_Integer (Result);
   end Get_Into_Vector_Long_Long_Integer;

   function Get_Into_Vector_Long_Float
     (This : in Statement;
      Position : in Into_Position;
      Index : in Vector_Index)
     return DB_Long_Float is

      function Soci_Get_Into_Double_V
        (St : in Statement_Handle;
         P : in Interfaces.C.int;
         I : in Interfaces.C.int)
        return Interfaces.C.double;
      pragma Import (C, Soci_Get_Into_Double_V, "soci_get_into_double_v");

      Result : constant Interfaces.C.double :=
        Soci_Get_Into_Double_V (This.Handle,
                                Interfaces.C.int (Position),
                                Interfaces.C.int (Index));

   begin
      Check_Statement_State (This.Handle);
      return DB_Long_Float (Result);
   end Get_Into_Vector_Long_Float;

   function Get_Into_Vector_Time
     (This : in Statement;
      Position : in Into_Position;
      Index : in Vector_Index)
     return Ada.Calendar.Time is

      function Soci_Get_Into_Date_V
        (St : in Statement_Handle;
         P : in Interfaces.C.int;
         I : in Interfaces.C.int)
        return Interfaces.C.Strings.chars_ptr;
      pragma Import (C, Soci_Get_Into_Date_V, "soci_get_into_date_v");

      Result_C : constant Interfaces.C.Strings.chars_ptr :=
        Soci_Get_Into_Date_V (This.Handle,
                              Interfaces.C.int (Position),
                              Interfaces.C.int (Index));
      Result : constant String := Interfaces.C.Strings.Value (Result_C);

   begin
      Check_Statement_State (This.Handle);
      return String_To_Time (Result);
   end Get_Into_Vector_Time;

   procedure Use_String (This : in Statement; Name : in String) is

      procedure Soci_Use_String
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array);
      pragma Import (C, Soci_Use_String, "soci_use_string");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);

   begin
      Soci_Use_String (This.Handle, Name_C);
      Check_Statement_State (This.Handle);
   end Use_String;

   procedure Use_Integer (This : in Statement; Name : in String) is

      procedure Soci_Use_Int
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array);
      pragma Import (C, Soci_Use_Int, "soci_use_int");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);

   begin
      Soci_Use_Int (This.Handle, Name_C);
      Check_Statement_State (This.Handle);
   end Use_Integer;

   procedure Use_Long_Long_Integer (This : in Statement; Name : in String) is

      procedure Soci_Use_Long_Long
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array);
      pragma Import (C, Soci_Use_Long_Long, "soci_use_long_long");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);

   begin
      Soci_Use_Long_Long (This.Handle, Name_C);
      Check_Statement_State (This.Handle);
   end Use_Long_Long_Integer;

   procedure Use_Long_Float (This : in Statement; Name : in String) is

      procedure Soci_Use_Double
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array);
      pragma Import (C, Soci_Use_Double, "soci_use_double");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);

   begin
      Soci_Use_Double (This.Handle, Name_C);
      Check_Statement_State (This.Handle);
   end Use_Long_Float;

   procedure Use_Time (This : in Statement; Name : in String) is

      procedure Soci_Use_Date
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array);
      pragma Import (C, Soci_Use_Date, "soci_use_date");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);

   begin
      Soci_Use_Date (This.Handle, Name_C);
      Check_Statement_State (This.Handle);
   end Use_Time;

   procedure Use_Vector_String (This : in Statement; Name : in String) is

      procedure Soci_Use_String_V
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array);
      pragma Import (C, Soci_Use_String_V, "soci_use_string_v");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);

   begin
      Soci_Use_String_V (This.Handle, Name_C);
      Check_Statement_State (This.Handle);
   end Use_Vector_String;

   procedure Use_Vector_Integer (This : in Statement; Name : in String) is

      procedure Soci_Use_Int_V
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array);
      pragma Import (C, Soci_Use_Int_V, "soci_use_int_v");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);

   begin
      Soci_Use_Int_V (This.Handle, Name_C);
      Check_Statement_State (This.Handle);
   end Use_Vector_Integer;

   procedure Use_Vector_Long_Long_Integer (This : in Statement; Name : in String) is

      procedure Soci_Use_Long_Long_V
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array);
      pragma Import (C, Soci_Use_Long_Long_V, "soci_use_long_long_v");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);

   begin
      Soci_Use_Long_Long_V (This.Handle, Name_C);
      Check_Statement_State (This.Handle);
   end Use_Vector_Long_Long_Integer;

   procedure Use_Vector_Long_Float (This : in Statement; Name : in String) is

      procedure Soci_Use_Double_V
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array);
      pragma Import (C, Soci_Use_Double_V, "soci_use_double_v");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);

   begin
      Soci_Use_Double_V (This.Handle, Name_C);
      Check_Statement_State (This.Handle);
   end Use_Vector_Long_Float;

   procedure Use_Vector_Time (This : in Statement; Name : in String) is

      procedure Soci_Use_Date_V
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array);
      pragma Import (C, Soci_Use_Date_V, "soci_use_date_v");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);

   begin
      Soci_Use_Date_V (This.Handle, Name_C);
      Check_Statement_State (This.Handle);
   end Use_Vector_Time;

   procedure Set_Use_State
     (This : in Statement;
      Name : in String;
      State : in Data_State) is

      procedure Soci_Set_Use_State
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         State : in Interfaces.C.int);
      pragma Import (C, Soci_Set_Use_State, "soci_set_use_state");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      State_C : constant Interfaces.C.int := Data_State_To_Int (State);

   begin
      Soci_Set_Use_State (This.Handle, Name_C, State_C);
      Check_Statement_State (This.Handle);
   end Set_Use_State;

   procedure Set_Use_String
     (This : in Statement;
      Name : in String;
      Value : in String) is

      procedure Soci_Set_Use_String
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         Value : in Interfaces.C.char_array);
      pragma Import (C, Soci_Set_Use_String, "soci_set_use_string");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Value_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Value);

   begin
      Soci_Set_Use_String (This.Handle, Name_C, Value_C);
      Check_Statement_State (This.Handle);
   end Set_Use_String;

   procedure Set_Use_Integer
     (This : in Statement;
      Name : in String;
      Value : in DB_Integer) is

      procedure Soci_Set_Use_Int
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         Value : in Interfaces.C.int);
      pragma Import (C, Soci_Set_Use_Int, "soci_set_use_int");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Value_C : constant Interfaces.C.int := Interfaces.C.int (Value);

   begin
      Soci_Set_Use_Int (This.Handle, Name_C, Value_C);
      Check_Statement_State (This.Handle);
   end Set_Use_Integer;

   procedure Set_Use_Long_Long_Integer
     (This : in Statement;
      Name : in String;
      Value : in DB_Long_Long_Integer) is

      procedure Soci_Set_Use_Long_Long
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         Value : in Interfaces.Integer_64);
      pragma Import (C, Soci_Set_Use_Long_Long, "soci_set_use_long_long");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Value_C : constant Interfaces.Integer_64 := Interfaces.Integer_64 (Value);

   begin
      Soci_Set_Use_Long_Long (This.Handle, Name_C, Value_C);
      Check_Statement_State (This.Handle);
   end Set_Use_Long_Long_Integer;

   procedure Set_Use_Long_Float
     (This : in Statement;
      Name : in String;
      Value : in DB_Long_Float) is

      procedure Soci_Set_Use_Double
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         Value : in Interfaces.C.double);
      pragma Import (C, Soci_Set_Use_Double, "soci_set_use_double");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Value_C : constant Interfaces.C.double := Interfaces.C.double (Value);

   begin
      Soci_Set_Use_Double (This.Handle, Name_C, Value_C);
      Check_Statement_State (This.Handle);
   end Set_Use_Long_Float;

   procedure Set_Use_Time
     (This : in Statement;
      Name : in String;
      Value : in Ada.Calendar.Time) is

      procedure Soci_Set_Use_Date
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         Value : in Interfaces.C.char_array);
      pragma Import (C, Soci_Set_Use_Date, "soci_set_use_date");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Value_C : constant Interfaces.C.char_array :=
        Interfaces.C.To_C (Time_To_String (Value));

   begin
      Soci_Set_Use_Date (This.Handle, Name_C, Value_C);
      Check_Statement_State (This.Handle);
   end Set_Use_Time;

   function Get_Use_Vectors_Size (This : in Statement) return Natural is

      function Soci_Use_Get_Size_V
        (St : in Statement_Handle)
        return Interfaces.C.int;
      pragma Import (C, Soci_Use_Get_Size_V, "soci_use_get_size_v");

      Result_C : constant Interfaces.C.int := Soci_Use_Get_Size_V (This.Handle);

   begin
      Check_Statement_State (This.Handle);
      return Natural (Result_C);
   end Get_Use_Vectors_Size;

   function Use_Vectors_First_Index (This : in Statement) return Vector_Index is
   begin
      return 0;
   end Use_Vectors_First_Index;

   function Use_Vectors_Last_Index (This : in Statement) return Vector_Index is
   begin
      return Vector_Index (This.Get_Use_Vectors_Size - 1);
   end Use_Vectors_Last_Index;

   procedure Use_Vectors_Resize (This : in Statement; New_Size : in Natural) is

      procedure Soci_Use_Resize_V
        (St : in Statement_Handle;
         New_Size : in Interfaces.C.int);
      pragma Import (C, Soci_Use_Resize_V, "soci_use_resize_v");

   begin
      Soci_Use_Resize_V (This.Handle, Interfaces.C.int (New_Size));
      Check_Statement_State (This.Handle);
   end Use_Vectors_Resize;

   procedure Set_Use_Vector_State
     (This : in Statement;
      Name : in String;
      Index : in Vector_Index;
      State : in Data_State) is

      procedure Soci_Set_Use_State_V
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         Index : in Interfaces.C.int;
         State : in Interfaces.C.int);
      pragma Import (C, Soci_Set_Use_State_V, "soci_set_use_state_v");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Index_C : constant Interfaces.C.int := Interfaces.C.int (Index);
      State_C : constant Interfaces.C.int := Data_State_To_Int (State);

   begin
      Soci_Set_Use_State_V (This.Handle, Name_C, Index_C, State_C);
      Check_Statement_State (This.Handle);
   end Set_Use_Vector_State;

   procedure Set_Use_Vector_String
     (This : in Statement;
      Name : in String;
      Index : in Vector_Index;
      Value : in String) is

      procedure Soci_Set_Use_String_V
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         Index : in Interfaces.C.int;
         Value : in Interfaces.C.char_array);
      pragma Import (C, Soci_Set_Use_String_V, "soci_set_use_string_v");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Index_C : constant Interfaces.C.int := Interfaces.C.int (Index);
      Value_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Value);

   begin
      Soci_Set_Use_String_V (This.Handle, Name_C, Index_C, Value_C);
      Check_Statement_State (This.Handle);
   end Set_Use_Vector_String;

   procedure Set_Use_Vector_Integer
     (This : in Statement;
      Name : in String;
      Index : in Vector_Index;
      Value : in DB_Integer) is

      procedure Soci_Set_Use_Int_V
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         Index : in Interfaces.C.int;
         Value : in Interfaces.C.int);
      pragma Import (C, Soci_Set_Use_Int_V, "soci_set_use_int_v");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Index_C : constant Interfaces.C.int := Interfaces.C.int (Index);
      Value_C : constant Interfaces.C.int := Interfaces.C.int (Value);

   begin
      Soci_Set_Use_Int_V (This.Handle, Name_C, Index_C, Value_C);
      Check_Statement_State (This.Handle);
   end Set_Use_Vector_Integer;

   procedure Set_Use_Vector_Long_Long_Integer
     (This : in Statement;
      Name : in String;
      Index : in Vector_Index;
      Value : in DB_Long_Long_Integer) is

      procedure Soci_Set_Use_Long_Long_V
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         Index : in Interfaces.C.int;
         Value : in Interfaces.Integer_64);
      pragma Import (C, Soci_Set_Use_Long_Long_V, "soci_set_use_long_long_v");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Index_C : constant Interfaces.C.int := Interfaces.C.int (Index);
      Value_C : constant Interfaces.Integer_64 := Interfaces.Integer_64 (Value);

   begin
      Soci_Set_Use_Long_Long_V (This.Handle, Name_C, Index_C, Value_C);
      Check_Statement_State (This.Handle);
   end Set_Use_Vector_Long_Long_Integer;

   procedure Set_Use_Vector_Long_Float
     (This : in Statement;
      Name : in String;
      Index : in Vector_Index;
      Value : in DB_Long_Float) is

      procedure Soci_Set_Use_Double_V
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         Index : in Interfaces.C.int;
         Value : in Interfaces.C.double);
      pragma Import (C, Soci_Set_Use_Double_V, "soci_set_use_double_v");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Index_C : constant Interfaces.C.int := Interfaces.C.int (Index);
      Value_C : constant Interfaces.C.double := Interfaces.C.double (Value);

   begin
      Soci_Set_Use_Double_V (This.Handle, Name_C, Index_C, Value_C);
      Check_Statement_State (This.Handle);
   end Set_Use_Vector_Long_Float;

   procedure Set_Use_Vector_Time
     (This : in Statement;
      Name : in String;
      Index : in Vector_Index;
      Value : in Ada.Calendar.Time) is

      procedure Soci_Set_Use_Date_V
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array;
         Index : in Interfaces.C.int;
         Value : in Interfaces.C.char_array);
      pragma Import (C, Soci_Set_Use_Date_V, "soci_set_use_date_v");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Index_C : constant Interfaces.C.int := Interfaces.C.int (Index);
      Value_C : constant Interfaces.C.char_array :=
        Interfaces.C.To_C (Time_To_String (Value));

   begin
      Soci_Set_Use_Date_V (This.Handle, Name_C, Index_C, Value_C);
      Check_Statement_State (This.Handle);
   end Set_Use_Vector_Time;

   function Get_Use_State
     (This : in Statement;
      Name : in String) return Data_State is

      function Soci_Get_Use_State
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array)
        return Interfaces.C.int;
      pragma Import (C, Soci_Get_Use_State, "soci_get_use_state");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Result : constant Interfaces.C.int :=
        Soci_Get_Use_State (This.Handle, Name_C);

      use type Interfaces.C.int;

   begin
      Check_Statement_State (This.Handle);
      return Int_To_Data_State (Result);
   end Get_Use_State;

   function Get_Use_String
     (This : in Statement;
      Name : in String)
     return String is

      function Soci_Get_Use_String
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array)
        return Interfaces.C.Strings.chars_ptr;
      pragma Import (C, Soci_Get_Use_String, "soci_get_use_string");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Result : constant Interfaces.C.Strings.chars_ptr :=
        Soci_Get_Use_String (This.Handle, Name_C);

   begin
      Check_Statement_State (This.Handle);
      return Interfaces.C.Strings.Value (Result);
   end Get_Use_String;

   function Get_Use_Integer
     (This : in Statement;
      Name : in String)
     return DB_Integer is

      function Soci_Get_Use_Int
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array)
        return Interfaces.C.int;
      pragma Import (C, Soci_Get_Use_Int, "soci_get_use_int");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Result : constant Interfaces.C.int :=
        Soci_Get_Use_Int (This.Handle, Name_C);

   begin
      Check_Statement_State (This.Handle);
      return DB_Integer (Result);
   end Get_Use_Integer;

   function Get_Use_Long_Long_Integer
     (This : in Statement;
      Name : in String)
     return DB_Long_Long_Integer is

      function Soci_Get_Use_Long_Long
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array)
        return Interfaces.Integer_64;
      pragma Import (C, Soci_Get_Use_Long_Long, "soci_get_use_long_long");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Result : constant Interfaces.Integer_64 :=
        Soci_Get_Use_Long_Long (This.Handle, Name_C);

   begin
      Check_Statement_State (This.Handle);
      return DB_Long_Long_Integer (Result);
   end Get_Use_Long_Long_Integer;

   function Get_Use_Long_Float
     (This : in Statement;
      Name : in String)
     return DB_Long_Float is

      function Soci_Get_Use_Double
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array)
        return Interfaces.C.double;
      pragma Import (C, Soci_Get_Use_Double, "soci_get_use_double");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Result : constant Interfaces.C.double :=
        Soci_Get_Use_Double (This.Handle, Name_C);

   begin
      Check_Statement_State (This.Handle);
      return DB_Long_Float (Result);
   end Get_Use_Long_Float;

   function Get_Use_Time
     (This : in Statement;
      Name : in String)
     return Ada.Calendar.Time is

      function Soci_Get_Use_Date
        (St : in Statement_Handle;
         Name : in Interfaces.C.char_array)
        return Interfaces.C.Strings.chars_ptr;
      pragma Import (C, Soci_Get_Use_Date, "soci_get_use_date");

      Name_C : constant Interfaces.C.char_array := Interfaces.C.To_C (Name);
      Result_C : constant Interfaces.C.Strings.chars_ptr :=
        Soci_Get_Use_Date (This.Handle, Name_C);
      Result : constant String := Interfaces.C.Strings.Value (Result_C);

   begin
      Check_Statement_State (This.Handle);
      return String_To_Time (Result);
   end Get_Use_Time;

end SOCI;
