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

#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

LedgerFormats::LedgerFormats()
{
    // Fields shared by all ledger formats:
    static const std::initializer_list<SOElement> commonFields{
        {sfLedgerIndex, soeOPTIONAL},
        {sfLedgerEntryType, soeREQUIRED},
        {sfFlags, soeREQUIRED},
    };

#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define UNWRAP(...) __VA_ARGS__
#define LEDGER_ENTRY(tag, value, name, fields) \
    add(jss::name, tag, UNWRAP fields, commonFields);

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
#undef UNWRAP
#pragma pop_macro("UNWRAP")
}

LedgerFormats const&
LedgerFormats::getInstance()
{
    static LedgerFormats instance;
    return instance;
}

}  // namespace ripple
