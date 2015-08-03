//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ODBC_SOURCE
#include "soci/odbc/soci-odbc.h"
#include "soci/backend-loader.h"

using namespace soci;
using namespace soci::details;


// concrete factory for ODBC concrete strategies
odbc_session_backend * odbc_backend_factory::make_session(
     connection_parameters const & parameters) const
{
     return new odbc_session_backend(parameters);
}

odbc_backend_factory const soci::odbc;

extern "C"
{

// for dynamic backend loading
SOCI_ODBC_DECL backend_factory const * factory_odbc()
{
    return &soci::odbc;
}

SOCI_ODBC_DECL void register_factory_odbc()
{
    soci::dynamic_backends::register_backend("odbc", soci::odbc);
}

} // extern "C"
