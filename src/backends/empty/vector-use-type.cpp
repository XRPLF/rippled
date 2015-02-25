//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_EMPTY_SOURCE
#include "soci-empty.h"

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;


void empty_vector_use_type_backend::bind_by_pos(int & /* position */,
        void * /* data */, exchange_type /* type */)
{
    // ...
}

void empty_vector_use_type_backend::bind_by_name(
    std::string const & /* name */, void * /* data */,
    exchange_type /* type */)
{
    // ...
}

void empty_vector_use_type_backend::pre_use(indicator const * /* ind */)
{
    // ...
}

std::size_t empty_vector_use_type_backend::size()
{
    // ...
    return 1;
}

void empty_vector_use_type_backend::clean_up()
{
    // ...
}
