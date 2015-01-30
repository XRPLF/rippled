//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton, Rafal Bobrowski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_FIREBIRD_SOURCE
#include "soci-firebird.h"

using namespace soci;

firebird_rowid_backend::firebird_rowid_backend(firebird_session_backend & /* session */)
{
    // Unsupported in Firebird backend
    throw soci_error("RowIDs are not supported");
}

firebird_rowid_backend::~firebird_rowid_backend()
{
}
