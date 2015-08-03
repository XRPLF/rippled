//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SQLITE3_SOURCE
#include "soci/sqlite3/soci-sqlite3.h"
#include "soci/backend-loader.h"

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;

// concrete factory for Empty concrete strategies
sqlite3_session_backend * sqlite3_backend_factory::make_session(
     connection_parameters const & parameters) const
{
     return new sqlite3_session_backend(parameters);
}

sqlite3_backend_factory const soci::sqlite3;

extern "C"
{

// for dynamic backend loading
SOCI_SQLITE3_DECL backend_factory const * factory_sqlite3()
{
    return &soci::sqlite3;
}

SOCI_SQLITE3_DECL void register_factory_sqlite3()
{
    soci::dynamic_backends::register_backend("sqlite3", soci::sqlite3);
}

} // extern "C"
