//
// Copyright (C) 2013 Mateusz Loskot <mateusz@loskot.net>
// Copyright (C) 2011-2013 Denis Chapligin
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef SOCI_DB2_COMMON_H_INCLUDED
#define SOCI_DB2_COMMON_H_INCLUDED

#include <cstddef>

namespace soci { namespace details { namespace db2 {

const std::size_t cli_max_buffer =  1024 * 1024 * 1024; //CLI limit is about 3 GB, but 1GB should be enough

}}} // namespace soci::details::db2

#endif // SOCI_DB2_COMMON_H_INCLUDED
