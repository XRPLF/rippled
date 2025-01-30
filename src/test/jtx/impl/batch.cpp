//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <test/jtx/batch.h>
#include <test/jtx/utility.h>
#include <xrpl/protocol/Batch.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <optional>
#include <sstream>

namespace ripple {
namespace test {
namespace jtx {

namespace batch {

XRPAmount
calcBatchFee(
    test::jtx::Env const& env,
    uint32_t const& numSigners,
    uint32_t const& txns)
{
    XRPAmount const feeDrops = env.current()->fees().base;
    return ((numSigners + 2) * feeDrops) + feeDrops * txns;
}

// Batch.
Json::Value
outer(
    jtx::Account const& account,
    uint32_t seq,
    STAmount const& fee,
    std::uint32_t flags)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::Batch;
    jv[jss::Account] = account.human();
    jv[jss::RawTransactions] = Json::Value{Json::arrayValue};
    jv[jss::Sequence] = seq;
    jv[jss::Flags] = flags;
    jv[jss::Fee] = to_string(fee);
    return jv;
}

void
inner_nofill::operator()(Env& env, JTx& jt) const
{
    auto const index = jt.jv[jss::RawTransactions].size();
    Json::Value& batchTransaction = jt.jv[jss::RawTransactions][index];
    batchTransaction = Json::Value{};
    batchTransaction[jss::RawTransaction] = txn_;
}

void
inner::operator()(Env& env, JTx& jt) const
{
    auto const index = jt.jv[jss::RawTransactions].size();
    Json::Value& batchTransaction = jt.jv[jss::RawTransactions][index];

    // Initialize the batch transaction
    batchTransaction = Json::Value{};
    batchTransaction[jss::RawTransaction] = txn_;
    batchTransaction[jss::RawTransaction][jss::SigningPubKey] = "";
    batchTransaction[jss::RawTransaction][jss::Sequence] = seq_;
    batchTransaction[jss::RawTransaction][jss::Fee] = "0";
    batchTransaction[jss::RawTransaction][jss::Flags] =
        batchTransaction[jss::RawTransaction][jss::Flags].asUInt() |
        tfInnerBatchTxn;

    // Optionally set ticket sequence
    if (ticket_.has_value())
    {
        batchTransaction[jss::RawTransaction][jss::Sequence] = 0;
        batchTransaction[jss::RawTransaction][sfTicketSequence.jsonName] =
            *ticket_;
    }
}

void
sig::operator()(Env& env, JTx& jt) const
{
    auto const mySigners = signers;
    std::optional<STObject> st;
    try
    {
        // required to cast the STObject to STTx
        jt.jv[jss::SigningPubKey] = "";
        st = parse(jt.jv);
    }
    catch (parse_error const&)
    {
        env.test.log << pretty(jt.jv) << std::endl;
        Rethrow();
    }
    STTx const& stx = STTx{std::move(*st)};
    auto& js = jt[sfBatchSigners.getJsonName()];
    for (std::size_t i = 0; i < mySigners.size(); ++i)
    {
        auto const& e = mySigners[i];
        auto& jo = js[i][sfBatchSigner.getJsonName()];
        jo[jss::Account] = e.acct.human();
        jo[jss::SigningPubKey] = strHex(e.sig.pk().slice());

        Serializer msg;
        serializeBatch(msg, stx.getFlags(), stx.getBatchTransactionIDs());
        auto const sig = ripple::sign(
            *publicKeyType(e.sig.pk().slice()), e.sig.sk(), msg.slice());
        jo[sfTxnSignature.getJsonName()] =
            strHex(Slice{sig.data(), sig.size()});
    }
}

void
msig::operator()(Env& env, JTx& jt) const
{
    auto const mySigners = signers;
    std::optional<STObject> st;
    try
    {
        // required to cast the STObject to STTx
        jt.jv[jss::SigningPubKey] = "";
        st = parse(jt.jv);
    }
    catch (parse_error const&)
    {
        env.test.log << pretty(jt.jv) << std::endl;
        Rethrow();
    }
    STTx const& stx = STTx{std::move(*st)};
    auto& bs = jt[sfBatchSigners.getJsonName()];
    auto const index = jt[sfBatchSigners.jsonName].size();
    auto& bso = bs[index][sfBatchSigner.getJsonName()];
    bso[jss::Account] = master.human();
    bso[jss::SigningPubKey] = "";
    auto& is = bso[sfSigners.getJsonName()];
    for (std::size_t i = 0; i < mySigners.size(); ++i)
    {
        auto const& e = mySigners[i];
        auto& iso = is[i][sfSigner.getJsonName()];
        iso[jss::Account] = e.acct.human();
        iso[jss::SigningPubKey] = strHex(e.sig.pk().slice());

        Serializer msg;
        serializeBatch(msg, stx.getFlags(), stx.getBatchTransactionIDs());
        auto const sig = ripple::sign(
            *publicKeyType(e.sig.pk().slice()), e.sig.sk(), msg.slice());
        iso[sfTxnSignature.getJsonName()] =
            strHex(Slice{sig.data(), sig.size()});
    }
}

}  // namespace batch

}  // namespace jtx
}  // namespace test
}  // namespace ripple
