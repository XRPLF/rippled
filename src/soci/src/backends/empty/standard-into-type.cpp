//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_EMPTY_SOURCE
#include "soci/empty/soci-empty.h"

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;


void empty_standard_into_type_backend::define_by_pos(
    int & /* position */, void * /* data */, exchange_type /* type */)
{
    // ...
}

void empty_standard_into_type_backend::pre_fetch()
{
    // ...
}

void empty_standard_into_type_backend::post_fetch(
    bool /* gotData */, bool /* calledFromFetch */, indicator * /* ind */)
{
    // ...
}

void empty_standard_into_type_backend::clean_up()
{
    // ...
}
