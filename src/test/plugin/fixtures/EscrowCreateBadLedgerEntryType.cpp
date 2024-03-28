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

#include <ripple/basics/Log.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/ledger/View.h>
#include <ripple/plugin/exports.h>
#include <ripple/plugin/macros.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/InnerObjectFormats.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/st.h>
#include <map>

using namespace ripple;

EXPORT_AMENDMENT_TEST(featurePluginTest2, true, VoteBehavior::DefaultNo);

static const std::uint16_t ltNEW_ESCROW = 0x0072;
static const std::uint16_t NEW_ESCROW_NAMESPACE = 't';

Keylet
new_escrow(AccountID const& src, std::uint32_t seq) noexcept
{
    return {ltNEW_ESCROW, indexHash(NEW_ESCROW_NAMESPACE, src, seq)};
}

NotTEC
preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
doApply(ApplyContext& ctx, XRPAmount mPriorBalance, XRPAmount mSourceBalance)
{
    return tesSUCCESS;
}

extern "C" Container<TransactorExport>
getTransactors()
{
    static SOElementExport format[] = {
        {sfDestination.getCode(), soeREQUIRED},
        {sfAmount.getCode(), soeREQUIRED},
        {sfCondition.getCode(), soeOPTIONAL},
        {sfCancelAfter.getCode(), soeOPTIONAL},
        {sfFinishAfter.getCode(), soeOPTIONAL},
        {sfDestinationTag.getCode(), soeOPTIONAL}};
    SOElementExport* formatPtr = format;
    static TransactorExport list[] = {
        {"NewEscrowCreate",
         51,
         {formatPtr, 6},
         ConsequencesFactoryType::Normal,
         NULL,
         NULL,
         preflight,
         preclaim,
         doApply,
         NULL,
         NULL,
         NULL,
         NULL}};
    TransactorExport* ptr = list;
    return {ptr, 1};
}

std::int64_t
visitEntryXRPChange(
    bool isDelete,
    std::shared_ptr<SLE const> const& entry,
    bool isBefore)
{
    return 0;
}

extern "C" Container<LedgerObjectExport>
getLedgerObjects()
{
    static SOElementExport format[] = {
        {sfAccount.getCode(), soeREQUIRED},
        {sfDestination.getCode(), soeREQUIRED},
        {sfAmount.getCode(), soeREQUIRED},
        {sfCondition.getCode(), soeOPTIONAL},
        {sfCancelAfter.getCode(), soeOPTIONAL},
        {sfFinishAfter.getCode(), soeOPTIONAL},
        {sfSourceTag.getCode(), soeOPTIONAL},
        {sfDestinationTag.getCode(), soeOPTIONAL},
        {sfOwnerNode.getCode(), soeREQUIRED},
        {sfPreviousTxnID.getCode(), soeREQUIRED},
        {sfPreviousTxnLgrSeq.getCode(), soeREQUIRED},
        {sfDestinationNode.getCode(), soeOPTIONAL},
    };
    static LedgerObjectExport list[] = {
        {ltNEW_ESCROW,
         "NewEscrow",
         "new_escrow",
         {format, 12},
         true,
         NULL,
         visitEntryXRPChange}};
    LedgerObjectExport* ptr = list;
    return {ptr, 1};
}

INITIALIZE_PLUGIN()
