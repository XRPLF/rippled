//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, David Courtney
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci-sqlite3.h"

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace soci;
using namespace soci::details;

sqlite3_rowid_backend::sqlite3_rowid_backend(
    sqlite3_session_backend & /* session */)
{
    // ...
}

sqlite3_rowid_backend::~sqlite3_rowid_backend()
{
    // ...
}
