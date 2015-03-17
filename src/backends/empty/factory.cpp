//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_EMPTY_SOURCE
#include "soci-empty.h"
#include <backend-loader.h>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;

// concrete factory for Empty concrete strategies
empty_session_backend* empty_backend_factory::make_session(
     connection_parameters const& parameters) const
{
     return new empty_session_backend(parameters);
}

empty_backend_factory const soci::empty;

extern "C"
{

// for dynamic backend loading
SOCI_EMPTY_DECL backend_factory const* factory_empty()
{
    return &soci::empty;
}

SOCI_EMPTY_DECL void register_factory_empty()
{
    soci::dynamic_backends::register_backend("empty", soci::empty);
}

} // extern "C"
