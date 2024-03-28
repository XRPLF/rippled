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

#ifndef RIPPLE_APP_TX_APPLYHANDLER_H_INCLUDED
#define RIPPLE_APP_TX_APPLYHANDLER_H_INCLUDED

#include <ripple/app/tx/applySteps.h>
#include <ripple/app/tx/impl/ApplyContext.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/plugin/exports.h>

namespace ripple {

class ApplyHandler
{
protected:
    ApplyContext& ctx;
    TransactorExport transactor_;

    XRPAmount mPriorBalance;   // Balance before fees.
    XRPAmount mSourceBalance;  // Balance after fees.

    ApplyHandler&
    operator=(ApplyHandler const&) = delete;

public:
    explicit ApplyHandler(ApplyContext& applyCtx, TransactorExport transactor);
    ApplyHandler(ApplyHandler const&) = delete;
    ~ApplyHandler() = default;

    enum ConsequencesFactoryType { Normal, Blocker, Custom };
    /** Process the transaction. */
    std::pair<TER, bool>
    operator()();

    // Interface used by DeleteAccount
    static TER
    ticketDelete(
        ApplyView& view,
        AccountID const& account,
        uint256 const& ticketIndex,
        beast::Journal j);

protected:
    TER
    apply();

    void
    preCompute();

private:
    std::pair<TER, XRPAmount>
    reset(XRPAmount fee);

    TER
    consumeSeqProxy(SLE::pointer const& sleAccount);
    TER
    payFee();
};

/** Performs early sanity checks on the txid */
NotTEC
preflight0(PreflightContext const& ctx);

/** Performs early sanity checks on the account and fee fields */
NotTEC
preflight1(PreflightContext const& ctx);

/** Checks whether the signature appears valid */
NotTEC
preflight2(PreflightContext const& ctx);

}  // namespace ripple

#endif
