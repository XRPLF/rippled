--  Copyright (C) 2008-2011 Maciej Sobczak
--  Distributed under the Boost Software License, Version 1.0.
--  (See accompanying file LICENSE_1_0.txt or copy at
--  http://www.boost.org/LICENSE_1_0.txt)

package SOCI.Oracle is

   --
   --  Registers the Oracle backend so that it is ready for use
   --  by the dynamic backend loader.
   --
   procedure Register_Factory_Oracle;
   pragma Import (C, Register_Factory_Oracle,
                  "register_factory_oracle");

end SOCI.Oracle;
