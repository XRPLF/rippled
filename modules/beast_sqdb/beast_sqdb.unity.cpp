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

#include <modules/beast_sqdb/beast_sqdb.h>

#include <modules/beast_sqlite/beast_sqlite.h>

#if BEAST_MSVC
#pragma warning (push)
#pragma warning (disable: 4100) // unreferenced formal parmaeter
#pragma warning (disable: 4355) // 'this' used in base member
#endif

// implementation headers
#include <modules/beast_sqdb/detail/error_codes.h>
#include <modules/beast_sqdb/detail/statement_imp.h>

#include <modules/beast_sqdb/source/blob.cpp>
#include <modules/beast_sqdb/source/error_codes.cpp>
#include <modules/beast_sqdb/source/into_type.cpp>
#include <modules/beast_sqdb/source/once_temp_type.cpp>
#include <modules/beast_sqdb/source/prepare_temp_type.cpp>
#include <modules/beast_sqdb/source/ref_counted_prepare_info.cpp>
#include <modules/beast_sqdb/source/ref_counted_statement.cpp>
#include <modules/beast_sqdb/source/session.cpp>
#include <modules/beast_sqdb/source/statement.cpp>
#include <modules/beast_sqdb/source/statement_imp.cpp>
#include <modules/beast_sqdb/source/transaction.cpp>
#include <modules/beast_sqdb/source/use_type.cpp>

#if BEAST_MSVC
#pragma warning (pop)
#endif
