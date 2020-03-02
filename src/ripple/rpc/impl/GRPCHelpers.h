//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_GRPCHELPERS_H_INCLUDED
#define RIPPLE_RPC_GRPCHELPERS_H_INCLUDED

#include "org/xrpl/rpc/v1/get_account_info.pb.h"
#include "org/xrpl/rpc/v1/ledger_objects.pb.h"
#include "org/xrpl/rpc/v1/meta.pb.h"
#include "org/xrpl/rpc/v1/transaction.pb.h"

#include <ripple/app/misc/TxQ.h>
#include <ripple/ledger/TxMeta.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STTx.h>

#include <functional>

namespace ripple {
namespace RPC {

void
convert(org::xrpl::rpc::v1::Meta& to, std::shared_ptr<TxMeta> const& from);

void
convert(
    org::xrpl::rpc::v1::QueueData& to,
    std::map<TxSeq, TxQ::AccountTxDetails const> const& from);

void
convert(
    org::xrpl::rpc::v1::Transaction& to,
    std::shared_ptr<STTx const> const& from);

void
convert(org::xrpl::rpc::v1::TransactionResult& to, TER from);

void
convert(org::xrpl::rpc::v1::AccountRoot& to, STObject const& from);

void
convert(org::xrpl::rpc::v1::SignerList& to, STObject const& from);

void
convert(org::xrpl::rpc::v1::NegativeUnl& to, STObject const& from);

template <class T>
void
convert(T& to, STAmount const& from)
{
    if (from.native())
    {
        to.mutable_value()->mutable_xrp_amount()->set_drops(from.xrp().drops());
    }
    else
    {
        Issue const& issue = from.issue();

        org::xrpl::rpc::v1::IssuedCurrencyAmount* issued =
            to.mutable_value()->mutable_issued_currency_amount();

        issued->mutable_currency()->set_name(to_string(issue.currency));
        issued->mutable_currency()->set_code(
            issue.currency.data(), Currency::size());
        issued->mutable_issuer()->set_address(toBase58(issue.account));
        issued->set_value(to_string(from.iou()));
    }
}

}  // namespace RPC
}  // namespace ripple

#endif
