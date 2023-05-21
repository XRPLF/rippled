//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_LEDGER_LEDGERHEADER_H_INCLUDED
#define RIPPLE_LEDGER_LEDGERHEADER_H_INCLUDED

#include <ripple/ledger/ReadView.h>

namespace ripple {

// We call them "headers" in conversation
// but "info" in code. Unintuitive.
// This alias makes the name easier to understand,
// without disturbing existing uses.
// and this header is easier to find.
// TODO: Move declaration here and rename all uses.
using LedgerHeader = LedgerInfo;

/** Deserialize a ledger header from a byte array. */
LedgerHeader
deserializeHeader(Slice data, bool hasHash = false);

/** Deserialize a ledger header (prefixed with 4 bytes) from a byte array. */
LedgerHeader
deserializePrefixedHeader(Slice data, bool hasHash = false);

}  // namespace ripple

#endif
