//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_EMPTY_SOURCE
#include "soci/empty/soci-empty.h"

using namespace soci;
using namespace soci::details;


void empty_vector_into_type_backend::define_by_pos(
    int & /* position */, void * /* data */, exchange_type /* type */)
{
    // ...
}

void empty_vector_into_type_backend::pre_fetch()
{
    // ...
}

void empty_vector_into_type_backend::post_fetch(
    bool /* gotData */, indicator * /* ind */)
{
    // ...
}

void empty_vector_into_type_backend::resize(std::size_t /* sz */)
{
    // ...
}

std::size_t empty_vector_into_type_backend::size()
{
    // ...
    return 1;
}

void empty_vector_into_type_backend::clean_up()
{
    // ...
}
