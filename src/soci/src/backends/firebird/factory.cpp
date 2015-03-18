//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, Rafal Bobrowski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_FIREBIRD_SOURCE
#include "soci-firebird.h"
#include <backend-loader.h>

using namespace soci;

firebird_session_backend * firebird_backend_factory::make_session(
    connection_parameters const & parameters) const
{
    return new firebird_session_backend(parameters);
}

firebird_backend_factory const soci::firebird;

extern "C"
{

// for dynamic backend loading
SOCI_FIREBIRD_DECL backend_factory const * factory_firebird()
{
    return &soci::firebird;
}

SOCI_FIREBIRD_DECL void register_factory_firebird()
{
    soci::dynamic_backends::register_backend("firebird", soci::firebird);
}

} // extern "C"
