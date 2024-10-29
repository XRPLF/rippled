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

#include <xrpl/protocol/TxFormats.h>

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

TxFormats::TxFormats()
{
    // Fields shared by all txFormats:
    static const std::initializer_list<SOElement> commonFields{
        {sfTransactionType, soeREQUIRED},
        {sfFlags, soeOPTIONAL},
        {sfSourceTag, soeOPTIONAL},
        {sfAccount, soeREQUIRED},
        {sfSequence, soeREQUIRED},
        {sfPreviousTxnID, soeOPTIONAL},  // emulate027
        {sfLastLedgerSequence, soeOPTIONAL},
        {sfAccountTxnID, soeOPTIONAL},
        {sfFee, soeREQUIRED},
        {sfOperationLimit, soeOPTIONAL},
        {sfMemos, soeOPTIONAL},
        {sfSigningPubKey, soeREQUIRED},
        {sfTicketSequence, soeOPTIONAL},
        {sfTxnSignature, soeOPTIONAL},
        {sfSigners, soeOPTIONAL},  // submit_multisigned
        {sfNetworkID, soeOPTIONAL},
    };

#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define UNWRAP(...) __VA_ARGS__
#define TRANSACTION(tag, value, name, fields) \
    add(jss::name, tag, UNWRAP fields, commonFields);

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
#undef UNWRAP
#pragma pop_macro("UNWRAP")
}

TxFormats const&
TxFormats::getInstance()
{
    static TxFormats const instance;
    return instance;
}

}  // namespace ripple
