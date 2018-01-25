//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "common.h"
#include <ciso646>
#include <cstdlib>
#include <cstring>
#include <ctime>

char * soci::details::mysql::quote(MYSQL * conn, const char *s, size_t len)
{
    char *retv = new char[2 * len + 3];
    retv[0] = '\'';
    int len_esc = mysql_real_escape_string(conn, retv + 1, s, static_cast<unsigned long>(len));
    retv[len_esc + 1] = '\'';
    retv[len_esc + 2] = '\0';

    return retv;
}
