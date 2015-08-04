//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// MySQL backend copyright (C) 2006 Pawel Aleksander Fedorynski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_MYSQL_SOURCE
#include "soci/mysql/soci-mysql.h"
#include "soci/backend-loader.h"
#include <ciso646>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;


// concrete factory for MySQL concrete strategies
mysql_session_backend * mysql_backend_factory::make_session(
     connection_parameters const & parameters) const
{
     return new mysql_session_backend(parameters);
}

mysql_backend_factory const soci::mysql;

extern "C"
{

// for dynamic backend loading
SOCI_MYSQL_DECL backend_factory const * factory_mysql()
{
    return &soci::mysql;
}

SOCI_MYSQL_DECL void register_factory_mysql()
{
    soci::dynamic_backends::register_backend("mysql", soci::mysql);
}

} // extern "C"
