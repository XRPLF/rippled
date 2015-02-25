###############################################################################
#
# This file is part of CMake configuration for SOCI library
#
# Copyright (C) 2010 Mateusz Loskot
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
###############################################################################
soci_backend(Empty
  HEADERS soci-empty.h
  DESCRIPTION "SOCI backend skeleton for development of new backends"
  AUTHORS "Maciej Sobczak, Stephen Hutton"
  MAINTAINERS "Maciej Sobczak")

add_subdirectory(test)
