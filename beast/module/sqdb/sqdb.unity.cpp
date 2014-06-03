//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#if BEAST_INCLUDE_BEASTCONFIG
#include <BeastConfig.h>
#endif

#include <beast/module/sqdb/sqdb.h>

#include <beast/module/sqlite/sqlite.h>

#if BEAST_MSVC
#pragma warning (push)
#pragma warning (disable: 4100) // unreferenced formal parmaeter
#pragma warning (disable: 4355) // 'this' used in base member
#endif

// implementation headers
#include <beast/module/sqdb/detail/error_codes.h>
#include <beast/module/sqdb/detail/statement_imp.h>

#include <beast/module/sqdb/source/blob.cpp>
#include <beast/module/sqdb/source/error_codes.cpp>
#include <beast/module/sqdb/source/into_type.cpp>
#include <beast/module/sqdb/source/once_temp_type.cpp>
#include <beast/module/sqdb/source/prepare_temp_type.cpp>
#include <beast/module/sqdb/source/ref_counted_prepare_info.cpp>
#include <beast/module/sqdb/source/ref_counted_statement.cpp>
#include <beast/module/sqdb/source/session.cpp>
#include <beast/module/sqdb/source/statement.cpp>
#include <beast/module/sqdb/source/statement_imp.cpp>
#include <beast/module/sqdb/source/transaction.cpp>
#include <beast/module/sqdb/source/use_type.cpp>

#if BEAST_MSVC
#pragma warning (pop)
#endif
