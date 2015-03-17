//
// Copyright (C) 2011-2013 Denis Chapligin
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_DB2_SOURCE
#include "soci-db2.h"
#include <backend-loader.h>

using namespace soci;
using namespace soci::details;


// concrete factory for ODBC concrete strategies
db2_session_backend * db2_backend_factory::make_session(
     connection_parameters const & parameters) const
{
     return new db2_session_backend(parameters);
}

db2_backend_factory const soci::db2;

extern "C"
{

// for dynamic backend loading
SOCI_DB2_DECL backend_factory const * factory_db2()
{
    return &soci::db2;
}

SOCI_DB2_DECL void register_factory_db2()
{
    soci::dynamic_backends::register_backend("db2", soci::db2);
}

} // extern "C"
