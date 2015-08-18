###############################################################################
#
# This file is part of CMake configuration for SOCI library
#
# Copyright (C) 2010-2013 Mateusz Loskot <mateusz@loskot.net>
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
###############################################################################

soci_backend_test(
  BACKEND Oracle
  DEPENDS Oracle
  SOURCE test-oracle.cpp ${SOCI_TESTS_COMMON}
  CONNSTR "service=orcl user=scott password=tiger")
