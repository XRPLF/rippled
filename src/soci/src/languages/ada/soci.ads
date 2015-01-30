--
--  Thin wrapper for the simple interface of the SOCI database access library.
--
--  Copyright (C) 2008-2011 Maciej Sobczak
--  Distributed under the Boost Software License, Version 1.0.
--  (See accompanying file LICENSE_1_0.txt or copy at
--  http://www.boost.org/LICENSE_1_0.txt)

with Ada.Calendar;
with Interfaces.C;

private with System;
private with Ada.Finalization;

package SOCI is

   --
   --  General exception related to database and library usage.
   --

   Database_Error : exception;

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

   --  Transaction management.

   not overriding
   procedure Start (This : in Session);

   not overriding
   procedure Commit (This : in Session);

   not overriding
   procedure Rollback (This : in Session);

   --  Immediate query execution.
   not overriding
   procedure Execute (This : in Session; Query : in String);

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

   --
   --  Statement.
   --

   type Statement (<>) is tagged limited private;

   type Data_State is (Data_Null, Data_Not_Null);

   type Into_Position is private;

   type Vector_Index is new Natural;

   not overriding
   function Make_Statement (Sess : in Session'Class) return Statement;

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

   --  Creation of vector into elements.

   not overriding
   function Into_Vector_String (This : in Statement) return Into_Position;

   not overriding
   function Into_Vector_Integer (This : in Statement) return Into_Position;

   not overriding
   function Into_Vector_Long_Long_Integer (This : in Statement) return Into_Position;

   not overriding
   function Into_Vector_Long_Float (This : in Statement) return Into_Position;

   not overriding
   function Into_Vector_Time (This : in Statement) return Into_Position;

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

   --  Inspection of vector into elements.

   not overriding
   function Get_Into_Vectors_Size (This : in Statement) return Natural;

   not overriding
   function Into_Vectors_First_Index (This : in Statement) return Vector_Index;

   not overriding
   function Into_Vectors_Last_Index (This : in Statement) return Vector_Index;

   not overriding
   procedure Into_Vectors_Resize (This : in Statement; New_Size : in Natural);

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

   --  Modifiers for vector use elements.

   not overriding
   function Get_Use_Vectors_Size (This : in Statement) return Natural;

   not overriding
   function Use_Vectors_First_Index (This : in Statement) return Vector_Index;

   not overriding
   function Use_Vectors_Last_Index (This : in Statement) return Vector_Index;

   not overriding
   procedure Use_Vectors_Resize (This : in Statement; New_Size : in Natural);

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

private

   --  Connection pool and supporting types.

   type Connection_Array is array (Positive range <>) of Session;
   type Used_Array is array (Positive range <>) of Boolean;

   --  Protected state for the connection pool.
   protected type Connection_Pool_PS (Size : Positive) is

      procedure Open (Position : in Positive; Connection_String : in String);
      procedure Close (Position : in Positive);

      entry Lease (S : in out Session'Class);
      procedure Give_Back (Position : in Positive);

   private

      Connections : Connection_Array (1 .. Size);
      Is_Used : Used_Array (1 .. Size) := (others => False);
      Available : Boolean := True;

   end Connection_Pool_PS;
   type Connection_Pool_PS_Ptr is access all Connection_Pool_PS;

   type Connection_Pool (Size : Positive) is tagged limited record
      Pool : aliased Connection_Pool_PS (Size);
   end record;

   --  Session and supporting types.

   type Session_Handle is new System.Address;

   Null_Session_Handle : constant Session_Handle :=
     Session_Handle (System.Null_Address);

   type Session is new Ada.Finalization.Limited_Controlled with record
      Handle : Session_Handle;
      Initialized : Boolean := False;
      Belongs_To_Pool : Boolean := False;
      Pool : Connection_Pool_PS_Ptr;
      Position_In_Pool : Positive;
   end record;

   overriding
   procedure Finalize (This : in out Session);

   --  Statement and supporting types.

   type Statement_Handle is new System.Address;

   Null_Statement_Handle : constant Statement_Handle :=
     Statement_Handle (System.Null_Address);

   type Statement is new Ada.Finalization.Limited_Controlled with record
      Handle : Statement_Handle;
      Initialized : Boolean := False;
   end record;

   overriding
   procedure Finalize (This : in out Statement);

   type Into_Position is new Natural;

end SOCI;
