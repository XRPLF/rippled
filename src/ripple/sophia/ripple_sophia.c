//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include "ripple_sophia.h"

#if RIPPLE_SOPHIA_AVAILABLE

#include "../sophia/db/cat.c"
#include "../sophia/db/crc.c"
#include "../sophia/db/cursor.c"
#include "../sophia/db/e.c"
#include "../sophia/db/file.c"
#include "../sophia/db/gc.c"
#include "../sophia/db/i.c"
#include "../sophia/db/merge.c"
#include "../sophia/db/recover.c"
#include "../sophia/db/rep.c"
#include "../sophia/db/sp.c"
#include "../sophia/db/util.c"

#endif
