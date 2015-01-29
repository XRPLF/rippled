###############################################################################
#
# This file is part of CMake configuration for SOCI library
#
# Copyright (C) 2011 Denis Chapligin
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
###############################################################################
soci_backend_test(
  BACKEND DB2
  SOURCE test-db2.cpp
  CONNSTR "DSN=SAMPLE;Uid=db2inst1;Pwd=db2inst1;autocommit=off")
