//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// MySQL backend copyright (C) 2006 Pawel Aleksander Fedorynski
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_MYSQL_SOURCE
#include "soci/mysql/soci-mysql.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4355 4702)
#endif

using namespace soci;
using namespace soci::details;

mysql_rowid_backend::mysql_rowid_backend(
    mysql_session_backend & /* session */)
{
    throw soci_error("RowIDs are not supported.");
}

mysql_rowid_backend::~mysql_rowid_backend()
{
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
