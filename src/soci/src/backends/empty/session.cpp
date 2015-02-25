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


empty_session_backend::empty_session_backend(
    connection_parameters const & /* parameters */)
{
    // ...
}

empty_session_backend::~empty_session_backend()
{
    clean_up();
}

void empty_session_backend::begin()
{
    // ...
}

void empty_session_backend::commit()
{
    // ...
}

void empty_session_backend::rollback()
{
    // ...
}

void empty_session_backend::clean_up()
{
    // ...
}

empty_statement_backend * empty_session_backend::make_statement_backend()
{
    return new empty_statement_backend(*this);
}

empty_rowid_backend * empty_session_backend::make_rowid_backend()
{
    return new empty_rowid_backend(*this);
}

empty_blob_backend * empty_session_backend::make_blob_backend()
{
    return new empty_blob_backend(*this);
}
