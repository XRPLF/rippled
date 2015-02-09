###############################################################################
#
# This file is part of CMake configuration for SOCI library
#
# Copyright (C) 2010 Mateusz Loskot <mateusz@loskot.net>
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
###############################################################################

soci_backend(SQLite3
  DEPENDS SQLite3
  HEADERS soci-sqlite3.h common.h
  DESCRIPTION "SOCI backend for SQLite 3 database engine"
  AUTHORS "Maciej Sobczak, Stephen Hutton, David Courtney"
  MAINTAINERS "Maciej Sobczak, Mateusz Loskot")

add_subdirectory(test)
