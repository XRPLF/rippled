//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_ODBC_SOURCE
#include "soci-odbc.h"

using namespace soci;
using namespace soci::details;


odbc_rowid_backend::odbc_rowid_backend(odbc_session_backend & /* session */)
{
    // ...
}

odbc_rowid_backend::~odbc_rowid_backend()
{
    // ...
}
