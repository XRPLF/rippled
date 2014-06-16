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


namespace ripple {

// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   ...
// }
Json::Value doLedgerEntry (RPC::Context& context)
{
    context.lock_.unlock ();

    Ledger::pointer     lpLedger;
    Json::Value         jvResult    = RPC::lookupLedger (context.params_, lpLedger, context.netOps_);

    if (!lpLedger)
        return jvResult;

    uint256     uNodeIndex;
    bool        bNodeBinary = false;

    if (context.params_.isMember ("index"))
    {
        // XXX Needs to provide proof.
        uNodeIndex.SetHex (context.params_["index"].asString ());
        bNodeBinary = true;
    }
    else if (context.params_.isMember ("account_root"))
    {
        RippleAddress   naAccount;

        if (!naAccount.setAccountID (context.params_["account_root"].asString ())
                || !naAccount.getAccountID ())
        {
            jvResult["error"]   = "malformedAddress";
        }
        else
        {
            uNodeIndex = Ledger::getAccountRootIndex (naAccount.getAccountID ());
        }
    }
    else if (context.params_.isMember ("directory"))
    {
        if (!context.params_["directory"].isObject ())
        {
            uNodeIndex.SetHex (context.params_["directory"].asString ());
        }
        else if (context.params_["directory"].isMember ("sub_index")
                 && !context.params_["directory"]["sub_index"].isIntegral ())
        {
            jvResult["error"]   = "malformedRequest";
        }
        else
        {
            std::uint64_t  uSubIndex = context.params_["directory"].isMember ("sub_index")
                                ? context.params_["directory"]["sub_index"].asUInt ()
                                : 0;

            if (context.params_["directory"].isMember ("dir_root"))
            {
                uint256 uDirRoot;

                uDirRoot.SetHex (context.params_["dir_root"].asString ());

                uNodeIndex  = Ledger::getDirNodeIndex (uDirRoot, uSubIndex);
            }
            else if (context.params_["directory"].isMember ("owner"))
            {
                RippleAddress   naOwnerID;

                if (!naOwnerID.setAccountID (context.params_["directory"]["owner"].asString ()))
                {
                    jvResult["error"]   = "malformedAddress";
                }
                else
                {
                    uint256 uDirRoot    = Ledger::getOwnerDirIndex (naOwnerID.getAccountID ());

                    uNodeIndex  = Ledger::getDirNodeIndex (uDirRoot, uSubIndex);
                }
            }
            else
            {
                jvResult["error"]   = "malformedRequest";
            }
        }
    }
    else if (context.params_.isMember ("generator"))
    {
        RippleAddress   naGeneratorID;

        if (!context.params_["generator"].isObject ())
        {
            uNodeIndex.SetHex (context.params_["generator"].asString ());
        }
        else if (!context.params_["generator"].isMember ("regular_seed"))
        {
            jvResult["error"]   = "malformedRequest";
        }
        else if (!naGeneratorID.setSeedGeneric (context.params_["generator"]["regular_seed"].asString ()))
        {
            jvResult["error"]   = "malformedAddress";
        }
        else
        {
            RippleAddress       na0Public;      // To find the generator's index.
            RippleAddress       naGenerator = RippleAddress::createGeneratorPublic (naGeneratorID);

            na0Public.setAccountPublic (naGenerator, 0);

            uNodeIndex  = Ledger::getGeneratorIndex (na0Public.getAccountID ());
        }
    }
    else if (context.params_.isMember ("offer"))
    {
        RippleAddress   naAccountID;

        if (!context.params_["offer"].isObject ())
        {
            uNodeIndex.SetHex (context.params_["offer"].asString ());
        }
        else if (!context.params_["offer"].isMember ("account")
                 || !context.params_["offer"].isMember ("seq")
                 || !context.params_["offer"]["seq"].isIntegral ())
        {
            jvResult["error"]   = "malformedRequest";
        }
        else if (!naAccountID.setAccountID (context.params_["offer"]["account"].asString ()))
        {
            jvResult["error"]   = "malformedAddress";
        }
        else
        {
            std::uint32_t      uSequence   = context.params_["offer"]["seq"].asUInt ();

            uNodeIndex  = Ledger::getOfferIndex (naAccountID.getAccountID (), uSequence);
        }
    }
    else if (context.params_.isMember ("ripple_state"))
    {
        RippleAddress   naA;
        RippleAddress   naB;
        uint160         uCurrency;
        Json::Value     jvRippleState   = context.params_["ripple_state"];

        if (!jvRippleState.isObject ()
                || !jvRippleState.isMember ("currency")
                || !jvRippleState.isMember ("accounts")
                || !jvRippleState["accounts"].isArray ()
                || 2 != jvRippleState["accounts"].size ()
                || !jvRippleState["accounts"][0u].isString ()
                || !jvRippleState["accounts"][1u].isString ()
                || jvRippleState["accounts"][0u].asString () == jvRippleState["accounts"][1u].asString ()
           )
        {
            jvResult["error"]   = "malformedRequest";
        }
        else if (!naA.setAccountID (jvRippleState["accounts"][0u].asString ())
                 || !naB.setAccountID (jvRippleState["accounts"][1u].asString ()))
        {
            jvResult["error"]   = "malformedAddress";
        }
        else if (!STAmount::currencyFromString (uCurrency, jvRippleState["currency"].asString ()))
        {
            jvResult["error"]   = "malformedCurrency";
        }
        else
        {
            uNodeIndex  = Ledger::getRippleStateIndex (naA, naB, uCurrency);
        }
    }
    else
    {
        jvResult["error"]   = "unknownOption";
    }

    if (uNodeIndex.isNonZero ())
    {
        SLE::pointer    sleNode = context.netOps_.getSLEi (lpLedger, uNodeIndex);

        if (context.params_.isMember("binary"))
            bNodeBinary = context.params_["binary"].asBool();

        if (!sleNode)
        {
            // Not found.
            // XXX Should also provide proof.
            jvResult["error"]       = "entryNotFound";
        }
        else if (bNodeBinary)
        {
            // XXX Should also provide proof.
            Serializer s;

            sleNode->add (s);

            jvResult["node_binary"] = strHex (s.peekData ());
            jvResult["index"]       = to_string (uNodeIndex);
        }
        else
        {
            jvResult["node"]        = sleNode->getJson (0);
            jvResult["index"]       = to_string (uNodeIndex);
        }
    }

    return jvResult;
}

} // ripple
