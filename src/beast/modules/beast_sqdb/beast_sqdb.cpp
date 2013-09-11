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

#include "BeastConfig.h"

#include "beast_sqdb.h"

#include "../beast_sqlite/beast_sqlite.h"

#if BEAST_MSVC
#pragma warning (push)
#pragma warning (disable: 4100) // unreferenced formal parmaeter
#pragma warning (disable: 4355) // 'this' used in base member
#endif

namespace beast
{
// implementation headers
#include "detail/error_codes.h"
#include "detail/statement_imp.h"
}

namespace beast
{
#include "source/blob.cpp"
#include "source/error_codes.cpp"
#include "source/into_type.cpp"
#include "source/once_temp_type.cpp"
#include "source/prepare_temp_type.cpp"
#include "source/ref_counted_prepare_info.cpp"
#include "source/ref_counted_statement.cpp"
#include "source/session.cpp"
#include "source/statement.cpp"
#include "source/statement_imp.cpp"
#include "source/transaction.cpp"
#include "source/use_type.cpp"
}

#if BEAST_MSVC
#pragma warning (pop)
#endif
