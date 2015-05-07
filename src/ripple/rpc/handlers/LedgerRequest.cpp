//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerToJson.h>

namespace ripple {

// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
// }
Json::Value doLedgerRequest (RPC::Context& context)
{
    auto const hasHash = context.params.isMember (jss::ledger_hash);
    auto const hasIndex = context.params.isMember (jss::ledger_index);

    auto& ledgerMaster = getApp().getLedgerMaster();
    LedgerHash ledgerHash;

    if ((hasHash && hasIndex) || !(hasHash || hasIndex))
    {
        return RPC::make_param_error(
            "Exactly one of ledger_hash and ledger_index can be set.");
    }

    if (hasHash)
    {
        auto const& jsonHash = context.params[jss::ledger_hash];
        if (!jsonHash.isString() || !ledgerHash.SetHex (jsonHash.asString ()))
            return RPC::invalid_field_message (jss::ledger_hash);
    } else {
        auto const& jsonIndex = context.params[jss::ledger_index];
        if (!jsonIndex.isNumeric ())
            return RPC::invalid_field_message (jss::ledger_index);

        // We need a validated ledger to get the hash from the sequence
        if (ledgerMaster.getValidatedLedgerAge() > 120)
            return rpcError (rpcNO_CURRENT);

        auto ledgerIndex = jsonIndex.asInt();
        auto ledger = ledgerMaster.getValidatedLedger();

        if (ledgerIndex >= ledger->getLedgerSeq())
            return RPC::make_param_error("Ledger index too large");

        // Try to get the hash of the desired ledger from the validated ledger
        ledgerHash = ledger->getLedgerHash (ledgerIndex);

        if (ledgerHash == zero)
        {
            // Find a ledger more likely to have the hash of the desired ledger
            auto refIndex = (ledgerIndex + 255) & (~255);
            auto refHash = ledger->getLedgerHash (refIndex);
            assert (refHash.isNonZero ());

            ledger = ledgerMaster.getLedgerByHash (refHash);
            if (!ledger)
            {
                // We don't have the ledger we need to figure out which ledger
                // they want. Try to get it.

                if (auto il = getApp().getInboundLedgers().acquire (
                        refHash, refIndex, InboundLedger::fcGENERIC))
                    return getJson (LedgerFill (*il));

                if (auto il = getApp().getInboundLedgers().find (refHash))
                {
                    Json::Value jvResult = il->getJson (0);

                    jvResult[jss::error] = "ledgerNotFound";
                    return jvResult;
                }

                // Likely the app is shutting down
                return Json::Value();
            }

            ledgerHash = ledger->getLedgerHash (ledgerIndex);
            assert (ledgerHash.isNonZero ());
        }
    }

    auto ledger = ledgerMaster.getLedgerByHash (ledgerHash);
    if (ledger)
    {
        // We already have the ledger they want
        Json::Value jvResult;
        jvResult[jss::ledger_index] = ledger->getLedgerSeq();
        addJson (jvResult, {*ledger, 0});
        return jvResult;
    }
    else
    {
        // Try to get the desired ledger
        if (auto il = getApp ().getInboundLedgers ().acquire (
                ledgerHash, 0, InboundLedger::fcGENERIC))
            return getJson (LedgerFill (*il));

        if (auto il = getApp().getInboundLedgers().find (ledgerHash))
            return il->getJson (0);

        return RPC::make_error (
            rpcNOT_READY, "findCreate failed to return an inbound ledger");
    }
}

} // ripple
