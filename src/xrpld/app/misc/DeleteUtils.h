//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_DELETEUTILS_H_INCLUDED
#define RIPPLE_APP_MISC_DELETEUTILS_H_INCLUDED

#include <xrpld/app/tx/detail/ApplyContext.h>
#include <xrpld/app/tx/detail/Transactor.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/TER.h>

namespace ripple {

// Define a function pointer type that can be used to delete ledger node types.
using DeleterFuncPtr = TER (*)(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j);

DeleterFuncPtr
nonObligationDeleter(LedgerEntryType t);

TER
deletePreclaim(
    PreclaimContext const& ctx,
    std::uint32_t seqDelta,
    AccountID const account,
    AccountID const dest,
    bool isPseudoAccount = false);

TER
deleteDoApply(
    ApplyContext& applyCtx,
    STAmount const& accountBalance,
    AccountID const& account,
    AccountID const& dest);

}  // namespace ripple

#endif
