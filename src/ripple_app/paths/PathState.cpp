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

namespace ripple {

// TODO:
// - Do automatic bridging via XRP.
//
// OPTIMIZE: When calculating path increment, note if increment consumes all liquidity. No need to revisit path in the future if
// all liquidity is used.
//

class RippleCalc; // for logging

std::size_t hash_value (const aciSource& asValue)
{
    std::size_t const seed = 0;
    return beast::hardened_hash<aciSource>{seed}(asValue);
}

// Compare the non-calculated fields.
bool PathState::Node::operator== (const Node& pnOther) const
{
    return pnOther.uFlags == uFlags
           && pnOther.uAccountID == uAccountID
           && pnOther.uCurrencyID == uCurrencyID
           && pnOther.uIssuerID == uIssuerID;
}

// This is for debugging not end users. Output names can be changed without warning.
Json::Value PathState::Node::getJson () const
{
    Json::Value jvNode (Json::objectValue);
    Json::Value jvFlags (Json::arrayValue);

    jvNode["type"]  = uFlags;

    if (is_bit_set (uFlags, STPathElement::typeAccount) || !!uAccountID)
        jvFlags.append (!!is_bit_set (uFlags, STPathElement::typeAccount) == !!uAccountID ? "account" : "-account");

    if (is_bit_set (uFlags, STPathElement::typeCurrency) || !!uCurrencyID)
        jvFlags.append (!!is_bit_set (uFlags, STPathElement::typeCurrency) == !!uCurrencyID ? "currency" : "-currency");

    if (is_bit_set (uFlags, STPathElement::typeIssuer) || !!uIssuerID)
        jvFlags.append (!!is_bit_set (uFlags, STPathElement::typeIssuer) == !!uIssuerID ? "issuer" : "-issuer");

    jvNode["flags"] = jvFlags;

    if (!!uAccountID)
        jvNode["account"]   = RippleAddress::createHumanAccountID (uAccountID);

    if (!!uCurrencyID)
        jvNode["currency"]  = STAmount::createHumanCurrency (uCurrencyID);

    if (!!uIssuerID)
        jvNode["issuer"]    = RippleAddress::createHumanAccountID (uIssuerID);

    if (saRevRedeem)
        jvNode["rev_redeem"]    = saRevRedeem.getFullText ();

    if (saRevIssue)
        jvNode["rev_issue"]     = saRevIssue.getFullText ();

    if (saRevDeliver)
        jvNode["rev_deliver"]   = saRevDeliver.getFullText ();

    if (saFwdRedeem)
        jvNode["fwd_redeem"]    = saFwdRedeem.getFullText ();

    if (saFwdIssue)
        jvNode["fwd_issue"]     = saFwdIssue.getFullText ();

    if (saFwdDeliver)
        jvNode["fwd_deliver"]   = saFwdDeliver.getFullText ();

    return jvNode;
}

//
// PathState implementation
//

// Return true, iff lhs has less priority than rhs.
bool PathState::lessPriority (PathState& lhs, PathState& rhs)
{
    // First rank is quality.
    if (lhs.uQuality != rhs.uQuality)
        return lhs.uQuality > rhs.uQuality;     // Bigger is worse.

    // Second rank is best quantity.
    if (lhs.saOutPass != rhs.saOutPass)
        return lhs.saOutPass < rhs.saOutPass;   // Smaller is worse.

    // Third rank is path index.
    return lhs.mIndex > rhs.mIndex;             // Bigger is worse.
}

// Make sure last path node delivers to uAccountID: uCurrencyID from uIssuerID.
//
// If the unadded next node as specified by arguments would not work as is, then add the necessary nodes so it would work.
//
// Rules:
// - Currencies must be converted via an offer.
// - A node names it's output.
// - A ripple nodes output issuer must be the node's account or the next node's account.
// - Offers can only go directly to another offer if the currency and issuer are an exact match.
// - Real issuers must be specified for non-XRP.
TER PathState::pushImply (
    const uint160& uAccountID,  // --> Delivering to this account.
    const uint160& uCurrencyID, // --> Delivering this currency.
    const uint160& uIssuerID)   // --> Delivering this issuer.
{
    const Node&  pnPrv       = vpnNodes.back ();
    TER          terResult   = tesSUCCESS;

    WriteLog (lsTRACE, RippleCalc) << "pushImply>" <<
        " " << RippleAddress::createHumanAccountID (uAccountID) <<
        " " << STAmount::createHumanCurrency (uCurrencyID) <<
        " " << RippleAddress::createHumanAccountID (uIssuerID);

    if (pnPrv.uCurrencyID != uCurrencyID)
    {
        // Currency is different, need to convert via an offer.

        terResult   = pushNode ( // Offer.
                          !!uCurrencyID
                          ? STPathElement::typeCurrency | STPathElement::typeIssuer
                          : STPathElement::typeCurrency,
                          ACCOUNT_XRP,                    // Placeholder for offers.
                          uCurrencyID,                    // The offer's output is what is now wanted.
                          uIssuerID);
    }

    const Node&  pnBck       = vpnNodes.back ();

    // For ripple, non-XRP, ensure the issuer is on at least one side of the transaction.
    if (tesSUCCESS == terResult
            && !!uCurrencyID                                // Not XRP.
            && (pnBck.uAccountID != uIssuerID               // Previous is not issuing own IOUs.
                && uAccountID != uIssuerID))                // Current is not receiving own IOUs.
    {
        // Need to ripple through uIssuerID's account.

        terResult   = pushNode (
                          STPathElement::typeAccount | STPathElement::typeCurrency | STPathElement::typeIssuer,
                          uIssuerID,                      // Intermediate account is the needed issuer.
                          uCurrencyID,
                          uIssuerID);
    }

    WriteLog (lsTRACE, RippleCalc) << "pushImply< : " << transToken (terResult);

    return terResult;
}

// Append a node and insert before it any implied nodes.
// Offers may go back to back.
// <-- terResult: tesSUCCESS, temBAD_PATH, terNO_ACCOUNT, terNO_AUTH, terNO_LINE, tecPATH_DRY
TER PathState::pushNode (
    const int iType,
    const uint160& uAccountID,
    const uint160& uCurrencyID,
    const uint160& uIssuerID)
{
    Node                pnCur;
    const bool          bFirst      = vpnNodes.empty ();
    const Node&         pnPrv       = bFirst ? Node () : vpnNodes.back ();
    // true, iff node is a ripple account. false, iff node is an offer node.
    const bool          bAccount    = is_bit_set (iType, STPathElement::typeAccount);
    // true, iff currency supplied.
    // Currency is specified for the output of the current node.
    const bool          bCurrency   = is_bit_set (iType, STPathElement::typeCurrency);
    // Issuer is specified for the output of the current node.
    const bool          bIssuer     = is_bit_set (iType, STPathElement::typeIssuer);
    TER                 terResult   = tesSUCCESS;

    WriteLog (lsTRACE, RippleCalc) << "pushNode> " <<
       iType <<
       ": " << (bAccount ? RippleAddress::createHumanAccountID (uAccountID) : "-") <<
       " " << (bCurrency ? STAmount::createHumanCurrency (uCurrencyID) : "-") <<
       "/" << (bIssuer ? RippleAddress::createHumanAccountID (uIssuerID) : "-");

    pnCur.uFlags        = iType;
    pnCur.uCurrencyID   = bCurrency ? uCurrencyID : pnPrv.uCurrencyID;

    if (iType & ~STPathElement::typeValidBits)
    {
        WriteLog (lsDEBUG, RippleCalc) << "pushNode: bad bits.";

        terResult   = temBAD_PATH;
    }
    else if (bIssuer && !pnCur.uCurrencyID)
    {
        WriteLog (lsDEBUG, RippleCalc) << "pushNode: issuer specified for XRP.";

        terResult   = temBAD_PATH;
    }
    else if (bIssuer && !uIssuerID)
    {
        WriteLog (lsDEBUG, RippleCalc) << "pushNode: specified bad issuer.";

        terResult   = temBAD_PATH;
    }
    else if (!bAccount && !bCurrency && !bIssuer)
    {
        WriteLog (lsDEBUG, RippleCalc) << "pushNode: offer must specify at least currency or issuer.";

        terResult   = temBAD_PATH;
    }
    else if (bAccount)
    {
        // Account link

        pnCur.uAccountID    = uAccountID;
        pnCur.uIssuerID     = bIssuer
                              ? uIssuerID
                              : !!pnCur.uCurrencyID
                              ? uAccountID
                              : ACCOUNT_XRP;
        pnCur.saRevRedeem   = STAmount (pnCur.uCurrencyID, uAccountID);
        pnCur.saRevIssue    = STAmount (pnCur.uCurrencyID, uAccountID);
        pnCur.saRevDeliver  = STAmount (pnCur.uCurrencyID, pnCur.uIssuerID);
        pnCur.saFwdDeliver  = pnCur.saRevDeliver;

        if (bFirst)
        {
            // The first node is always correct as is.

            nothing ();
        }
        else if (!uAccountID)
        {
            WriteLog (lsDEBUG, RippleCalc) << "pushNode: specified bad account.";

            terResult   = temBAD_PATH;
        }
        else
        {
            // Add required intermediate nodes to deliver to current account.
            WriteLog (lsTRACE, RippleCalc) << "pushNode: imply for account.";

            terResult   = pushImply (
                              pnCur.uAccountID,                                   // Current account.
                              pnCur.uCurrencyID,                                  // Wanted currency.
                              !!pnCur.uCurrencyID ? uAccountID : ACCOUNT_XRP);    // Account as wanted issuer.

            // Note: pnPrv may no longer be the immediately previous node.
        }

        if (tesSUCCESS == terResult && !vpnNodes.empty ())
        {
            const Node&     pnBck       = vpnNodes.back ();
            bool            bBckAccount = is_bit_set (pnBck.uFlags, STPathElement::typeAccount);

            if (bBckAccount)
            {
                SLE::pointer    sleRippleState  = lesEntries.entryCache (ltRIPPLE_STATE, Ledger::getRippleStateIndex (pnBck.uAccountID, pnCur.uAccountID, pnPrv.uCurrencyID));

                if (!sleRippleState)
                {
                    WriteLog (lsTRACE, RippleCalc) << "pushNode: No credit line between "
                                                   << RippleAddress::createHumanAccountID (pnBck.uAccountID)
                                                   << " and "
                                                   << RippleAddress::createHumanAccountID (pnCur.uAccountID)
                                                   << " for "
                                                   << STAmount::createHumanCurrency (pnCur.uCurrencyID)
                                                   << "." ;

                    WriteLog (lsTRACE, RippleCalc) << getJson ();

                    terResult   = terNO_LINE;
                }
                else
                {
                    WriteLog (lsTRACE, RippleCalc) << "pushNode: Credit line found between "
                                                   << RippleAddress::createHumanAccountID (pnBck.uAccountID)
                                                   << " and "
                                                   << RippleAddress::createHumanAccountID (pnCur.uAccountID)
                                                   << " for "
                                                   << STAmount::createHumanCurrency (pnCur.uCurrencyID)
                                                   << "." ;

                    SLE::pointer        sleBck  = lesEntries.entryCache (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (pnBck.uAccountID));
                    bool                bHigh   = pnBck.uAccountID > pnCur.uAccountID;

                    if (!sleBck)
                    {
                        WriteLog (lsWARNING, RippleCalc) << "pushNode: delay: can't receive IOUs from non-existent issuer: " << RippleAddress::createHumanAccountID (pnBck.uAccountID);

                        terResult   = terNO_ACCOUNT;
                    }
                    else if ((is_bit_set (sleBck->getFieldU32 (sfFlags), lsfRequireAuth)
                             && !is_bit_set (sleRippleState->getFieldU32 (sfFlags), (bHigh ? lsfHighAuth : lsfLowAuth)))
                             && sleRippleState->getFieldAmount(sfBalance) == zero) // CHECKME
                    {
                        WriteLog (lsWARNING, RippleCalc) << "pushNode: delay: can't receive IOUs from issuer without auth.";

                        terResult   = terNO_AUTH;
                    }

                    if (tesSUCCESS == terResult)
                    {
                        STAmount    saOwed  = lesEntries.rippleOwed (pnCur.uAccountID, pnBck.uAccountID, pnCur.uCurrencyID);
                        STAmount    saLimit;

                        if (saOwed <= zero
                                && -saOwed >= (saLimit = lesEntries.rippleLimit (pnCur.uAccountID, pnBck.uAccountID, pnCur.uCurrencyID)))
                        {
                            WriteLog (lsWARNING, RippleCalc) <<
                                "pushNode: dry:" <<
                                " saOwed=" << saOwed <<
                                " saLimit=" << saLimit;

                            terResult   = tecPATH_DRY;
                        }
                    }
                }
            }
        }

        if (tesSUCCESS == terResult)
        {
            vpnNodes.push_back (pnCur);
        }
    }
    else
    {
        // Offer link
        // Offers bridge a change in currency & issuer or just a change in issuer.
        pnCur.uIssuerID     = bIssuer
                              ? uIssuerID
                              : !!pnCur.uCurrencyID
                              ? !!pnPrv.uIssuerID
                              ? pnPrv.uIssuerID   // Default to previous issuer
                              : pnPrv.uAccountID  // Or previous account if no previous issuer.
                      : ACCOUNT_XRP;
        pnCur.saRateMax     = saZero;
        pnCur.saRevDeliver  = STAmount (pnCur.uCurrencyID, pnCur.uIssuerID);
        pnCur.saFwdDeliver  = pnCur.saRevDeliver;

        if (!!pnCur.uCurrencyID != !!pnCur.uIssuerID)
        {
            WriteLog (lsDEBUG, RippleCalc) << "pushNode: currency is inconsistent with issuer.";

            terResult   = temBAD_PATH;
        }
        else if (!!pnPrv.uAccountID)
        {
            // Previous is an account.
            WriteLog (lsTRACE, RippleCalc) << "pushNode: imply for offer.";

            // Insert intermediary issuer account if needed.
            terResult   = pushImply (
                ACCOUNT_XRP, // Rippling, but offers don't have an account.
                pnPrv.uCurrencyID,
                pnPrv.uIssuerID);
        }

        if (tesSUCCESS == terResult)
        {
            vpnNodes.push_back (pnCur);
        }
    }

    WriteLog (lsTRACE, RippleCalc) << "pushNode< : " << transToken (terResult);

    return terResult;
}

// Set to an expanded path.
//
// terStatus = tesSUCCESS, temBAD_PATH, terNO_LINE, terNO_ACCOUNT, terNO_AUTH, or temBAD_PATH_LOOP
void PathState::setExpanded (
    const LedgerEntrySet&   lesSource,
    const STPath&           spSourcePath,
    const uint160&          uReceiverID,
    const uint160&          uSenderID
)
{
    uQuality    = 1;            // Mark path as active.

    const uint160   uMaxCurrencyID  = saInReq.getCurrency ();
    const uint160   uMaxIssuerID    = saInReq.getIssuer ();

    const uint160   uOutCurrencyID  = saOutReq.getCurrency ();
    const uint160   uOutIssuerID    = saOutReq.getIssuer ();
    const uint160   uSenderIssuerID = !!uMaxCurrencyID ? uSenderID : ACCOUNT_XRP;   // Sender is always issuer for non-XRP.

    WriteLog (lsTRACE, RippleCalc) << "setExpanded> " << spSourcePath.getJson (0);

    lesEntries  = lesSource.duplicate ();

    terStatus   = tesSUCCESS;

    // XRP with issuer is malformed.
    if ((!uMaxCurrencyID && !!uMaxIssuerID) || (!uOutCurrencyID && !!uOutIssuerID))
        terStatus   = temBAD_PATH;

    // Push sending node.
    // For non-XRP, issuer is always sending account.
    // - Trying to expand, not-compact.
    // - Every issuer will be traversed through.
    if (tesSUCCESS == terStatus)
        terStatus   = pushNode (
                          !!uMaxCurrencyID
                          ? STPathElement::typeAccount | STPathElement::typeCurrency | STPathElement::typeIssuer
                          : STPathElement::typeAccount | STPathElement::typeCurrency,
                          uSenderID,
                          uMaxCurrencyID,                                 // Max specifies the currency.
                          uSenderIssuerID);

    WriteLog (lsDEBUG, RippleCalc) << "setExpanded: pushed:" <<
        " account=" << RippleAddress::createHumanAccountID (uSenderID) <<
        " currency=" << STAmount::createHumanCurrency (uMaxCurrencyID) <<
        " issuer=" << RippleAddress::createHumanAccountID (uSenderIssuerID);

    if (tesSUCCESS == terStatus
            && uMaxIssuerID != uSenderIssuerID)                 // Issuer was not same as sender.
    {
        // May have an implied account node.
        // - If it was XRP, then issuers would have matched.

        // Figure out next node properties for implied node.
        const uint160   uNxtCurrencyID  = spSourcePath.size ()
                                          ? spSourcePath.getElement (0).getCurrency () // Use next node.
                                          : uOutCurrencyID;                           // Use send.
        const uint160   uNxtAccountID   = spSourcePath.size ()
                                          ? spSourcePath.getElement (0).getAccountID ()
                                          : !!uOutCurrencyID
                                          ? uOutIssuerID == uReceiverID
                                          ? uReceiverID
                                          : uOutIssuerID                      // Use implied node.
                                  : ACCOUNT_XRP;

        WriteLog (lsDEBUG, RippleCalc) << "setExpanded: implied check:" <<
            " uMaxIssuerID=" << RippleAddress::createHumanAccountID (uMaxIssuerID) <<
            " uSenderIssuerID=" << RippleAddress::createHumanAccountID (uSenderIssuerID) <<
            " uNxtCurrencyID=" << STAmount::createHumanCurrency (uNxtCurrencyID) <<
            " uNxtAccountID=" << RippleAddress::createHumanAccountID (uNxtAccountID);

        // Can't just use push implied, because it can't compensate for next account.
        if (!uNxtCurrencyID                         // Next is XRP, offer next. Must go through issuer.
                || uMaxCurrencyID != uNxtCurrencyID // Next is different currency, offer next...
                || uMaxIssuerID != uNxtAccountID)   // Next is not implied issuer
        {
            WriteLog (lsDEBUG, RippleCalc) << "setExpanded: sender implied:" <<
                " account=" << RippleAddress::createHumanAccountID (uMaxIssuerID) <<
                " currency=" << STAmount::createHumanCurrency (uMaxCurrencyID) <<
                " issuer=" << RippleAddress::createHumanAccountID (uMaxIssuerID);

            // Add account implied by SendMax.
            terStatus   = pushNode (
                !!uMaxCurrencyID
                    ? STPathElement::typeAccount | STPathElement::typeCurrency | STPathElement::typeIssuer
                    : STPathElement::typeAccount | STPathElement::typeCurrency,
                uMaxIssuerID,
                uMaxCurrencyID,
                uMaxIssuerID);
        }
    }

    BOOST_FOREACH (const STPathElement & speElement, spSourcePath)
    {
        if (tesSUCCESS == terStatus)
        {
            WriteLog (lsTRACE, RippleCalc) << "setExpanded: element in path";
            terStatus   = pushNode (
                speElement.getNodeType (), speElement.getAccountID (), 
                speElement.getCurrency (), speElement.getIssuerID ());
        }
    }

    const Node&  pnPrv           = vpnNodes.back ();

    if (tesSUCCESS == terStatus
            && !!uOutCurrencyID                         // Next is not XRP
            && uOutIssuerID != uReceiverID              // Out issuer is not receiver
            && (pnPrv.uCurrencyID != uOutCurrencyID     // Previous will be an offer.
                || pnPrv.uAccountID != uOutIssuerID))   // Need the implied issuer.
    {
        // Add implied account.
        WriteLog (lsDEBUG, RippleCalc) << "setExpanded: receiver implied:" <<
            " account=" << RippleAddress::createHumanAccountID (uOutIssuerID) <<
            " currency=" << STAmount::createHumanCurrency (uOutCurrencyID) <<
            " issuer=" << RippleAddress::createHumanAccountID (uOutIssuerID);

        terStatus   = pushNode (
            !!uOutCurrencyID
                ? STPathElement::typeAccount | STPathElement::typeCurrency | STPathElement::typeIssuer
                : STPathElement::typeAccount | STPathElement::typeCurrency,
            uOutIssuerID,
            uOutCurrencyID,
            uOutIssuerID);
    }

    if (tesSUCCESS == terStatus)
    {
        // Create receiver node.
        // Last node is always an account.

        terStatus   = pushNode (
            !!uOutCurrencyID
                ? STPathElement::typeAccount | STPathElement::typeCurrency | STPathElement::typeIssuer
                : STPathElement::typeAccount | STPathElement::typeCurrency,
            uReceiverID,                                    // Receive to output
            uOutCurrencyID,                                 // Desired currency
            uReceiverID);
    }

    if (tesSUCCESS == terStatus)
    {
        // Look for first mention of source in nodes and detect loops.
        // Note: The output is not allowed to be a source.

        const unsigned int  uNodes  = vpnNodes.size ();

        for (unsigned int uNode = 0; tesSUCCESS == terStatus && uNode != uNodes; ++uNode)
        {
            const Node&  pnCur   = vpnNodes[uNode];

            if (!umForward.insert (std::make_pair (std::make_tuple (pnCur.uAccountID, pnCur.uCurrencyID, pnCur.uIssuerID), uNode)).second)
            {
                // Failed to insert. Have a loop.
                WriteLog (lsDEBUG, RippleCalc) <<
                    "setExpanded: loop detected: " << getJson ();

                terStatus   = temBAD_PATH_LOOP;
            }
        }
    }

    WriteLog (lsDEBUG, RippleCalc) << "setExpanded:" <<
        " in=" << STAmount::createHumanCurrency (uMaxCurrencyID) <<
        "/" << RippleAddress::createHumanAccountID (uMaxIssuerID) <<
        " out=" << STAmount::createHumanCurrency (uOutCurrencyID) <<
        "/" << RippleAddress::createHumanAccountID (uOutIssuerID) <<
        ": " << getJson ();
}

// Set to a canonical path.
// - Remove extra elements
// - Assumes path is expanded.
//
// We do canonicalization to:
// - Prevent waste in the ledger.
// - Allow longer paths to be specified than would otherwise be allowed.
//
// Optimization theory:
// - Can omit elements that the expansion routine derives.
// - Can pack some elements into other elements.
//
// Rules:
// - SendMax if not specified, defaults currency to send and if not sending XRP defaults issuer to sender.
// - All paths start with the sender account.
//   - Currency and issuer is from SendMax.
// - All paths end with the destination account.
//
// Optimization:
// - An XRP output implies an offer node or destination node is next.
// - A change in currency implies an offer node.
// - A change in issuer...
void PathState::setCanonical (
    const PathState&    psExpanded
)
{
    assert (false);
    saInAct     = psExpanded.saInAct;
    saOutAct    = psExpanded.saOutAct;

    const uint160   uMaxCurrencyID  = saInAct.getCurrency ();
    const uint160   uMaxIssuerID    = saInAct.getIssuer ();

    const uint160   uOutCurrencyID  = saOutAct.getCurrency ();
    const uint160   uOutIssuerID    = saOutAct.getIssuer ();

    unsigned int    uNode       = 0;

    unsigned int    uEnd        = psExpanded.vpnNodes.size ();  // The node, indexed by 0, not to include.

    uint160         uDstAccountID   = psExpanded.vpnNodes[uEnd].uAccountID; // FIXME: This can't be right

    uint160         uAccountID      = psExpanded.vpnNodes[0].uAccountID;
    uint160         uCurrencyID     = uMaxCurrencyID;
    uint160         uIssuerID       = uMaxIssuerID;

    // Node 0 is a composite of the sending account and saInAct.
    ++uNode;    // skip node 0

    // Last node is implied: Always skip last node
    --uEnd;     // skip last node

    // saInAct
    // - currency is always the same as vpnNodes[0].
#if 1

    if (uNode != uEnd && uMaxIssuerID != uAccountID)
    {
        // saInAct issuer is not the sender. This forces an implied node.
        // WriteLog (lsDEBUG, RippleCalc) << boost::str(boost::format("setCanonical: in diff: uNode=%d uEnd=%d") % uNode % uEnd);

        // skip node 1

        uIssuerID   = psExpanded.vpnNodes[uNode].uIssuerID;

        ++uNode;
    }

#else

    if (uNode != uEnd)
    {
        // Have another node
        bool    bKeep   = false;

        if (uMaxIssuerID != uAccountID)
        {
        }

        if (uMaxCurrencyID)                         // Not sending XRP.
        {
            // Node 1 must be an account.

            if (uMaxIssuerID != uAccountID)
            {
                // Node 1 is required to specify issuer.

                bKeep   = true;
            }
            else
            {
                // Node 1 must be an account
            }
        }
        else
        {
            // Node 1 must be an order book.

            bKeep           = true;
        }

        if (bKeep)
        {
            uCurrencyID = psExpanded.vpnNodes[uNode].uCurrencyID;
            uIssuerID   = psExpanded.vpnNodes[uNode].uIssuerID;
            ++uNode;        // Keep it.
        }
    }

#endif

    if (uNode != uEnd && !!uOutCurrencyID && uOutIssuerID != uDstAccountID)
    {
        // WriteLog (lsDEBUG, RippleCalc) << boost::str(boost::format("setCanonical: out diff: uNode=%d uEnd=%d") % uNode % uEnd);
        // The next to last node is saOutAct if an issuer different from receiver is supplied.
        // The next to last node can be implied.

        --uEnd;
    }

    const Node&  pnEnd   = psExpanded.vpnNodes[uEnd];

    if (uNode != uEnd
            && !pnEnd.uAccountID && pnEnd.uCurrencyID == uOutCurrencyID && pnEnd.uIssuerID == uOutIssuerID)
    {
        // The current end node is an offer converting to saOutAct's currency and issuer and can be implied.
        // WriteLog (lsDEBUG, RippleCalc) << boost::str(boost::format("setCanonical: out offer: uNode=%d uEnd=%d") % uNode % uEnd);

        --uEnd;
    }

    // Do not include uEnd.
    for (; uNode != uEnd; ++uNode)
    {
        // WriteLog (lsDEBUG, RippleCalc) << boost::str(boost::format("setCanonical: loop: uNode=%d uEnd=%d") % uNode % uEnd);
        const Node&  pnPrv   = psExpanded.vpnNodes[uNode - 1];
        const Node&  pnCur   = psExpanded.vpnNodes[uNode];
        const Node&  pnNxt   = psExpanded.vpnNodes[uNode + 1];

        const bool      bCurAccount     = is_bit_set (pnCur.uFlags, STPathElement::typeAccount);

        bool            bSkip   = false;

        if (bCurAccount)
        {
            // Currently at an account.

            // Output is non-XRP and issuer is account.
            if (!!pnCur.uCurrencyID && pnCur.uIssuerID == pnCur.uAccountID)
            {
                // Account issues itself.
                // XXX Not good enough. Previous account must mention it.

                bSkip   = true;
            }
        }
        else
        {
            // Currently at an offer.
            const bool      bPrvAccount     = is_bit_set (pnPrv.uFlags, STPathElement::typeAccount);
            const bool      bNxtAccount     = is_bit_set (pnNxt.uFlags, STPathElement::typeAccount);

            if (bPrvAccount && bNxtAccount                  // Offer surrounded by accounts.
                    && pnPrv.uCurrencyID != pnNxt.uCurrencyID)
            {
                // Offer can be implied by currency change.
                // XXX What about issuer?

                bSkip   = true;
            }
        }

        if (!bSkip)
        {
            // Copy node
            Node     pnNew;

            bool            bSetAccount     = bCurAccount;
            bool            bSetCurrency    = uCurrencyID != pnCur.uCurrencyID;
            // XXX What if we need the next account because we want to skip it?
            bool            bSetIssuer      = !uCurrencyID && uIssuerID != pnCur.uIssuerID;

            pnNew.uFlags    = (bSetAccount ? STPathElement::typeAccount : 0)
                              | (bSetCurrency ? STPathElement::typeCurrency : 0)
                              | (bSetIssuer ? STPathElement::typeIssuer : 0);

            if (bSetAccount)
                pnNew.uAccountID    = pnCur.uAccountID;

            if (bSetCurrency)
            {
                pnNew.uCurrencyID   = pnCur.uCurrencyID;
                uCurrencyID         = pnNew.uCurrencyID;
            }

            if (bSetIssuer)
                pnNew.uIssuerID     = pnCur.uIssuerID;

            // XXX ^^^ What about setting uIssuerID?

            if (bSetCurrency && !uCurrencyID)
                uIssuerID.zero ();

            vpnNodes.push_back (pnNew);
        }
    }

    WriteLog (lsDEBUG, RippleCalc) << "setCanonical:" <<
        " in=" << STAmount::createHumanCurrency (uMaxCurrencyID) <<
        "/" << RippleAddress::createHumanAccountID (uMaxIssuerID) <<
        " out=" << STAmount::createHumanCurrency (uOutCurrencyID) <<
        "/" << RippleAddress::createHumanAccountID (uOutIssuerID) <<
        ": " << getJson ();
}

/** Check if a sequence of three accounts violates the no ripple constrains
    [first] -> [second] -> [third]
    Disallowed if 'second' set no ripple on [first]->[second] and [second]->[third]
*/
void PathState::checkNoRipple (
    uint160 const& firstAccount,
    uint160 const& secondAccount,  // This is the account whose constraints we are checking
    uint160 const& thirdAccount,
    uint160 const& currency)
{
    // fetch the ripple lines into and out of this node
    SLE::pointer sleIn = lesEntries.entryCache (ltRIPPLE_STATE,
        Ledger::getRippleStateIndex (firstAccount, secondAccount, currency));
    SLE::pointer sleOut = lesEntries.entryCache (ltRIPPLE_STATE,
        Ledger::getRippleStateIndex (secondAccount, thirdAccount, currency));

    if (!sleIn || !sleOut)
    {
        terStatus = terNO_LINE;
    }
    else if (
        is_bit_set (sleIn->getFieldU32 (sfFlags),
            (secondAccount > firstAccount) ? lsfHighNoRipple : lsfLowNoRipple) &&
        is_bit_set (sleOut->getFieldU32 (sfFlags),
            (secondAccount > thirdAccount) ? lsfHighNoRipple : lsfLowNoRipple))
    {
        WriteLog (lsINFO, RippleCalc) << "Path violates noRipple constraint between " <<
            RippleAddress::createHumanAccountID (firstAccount) << ", " <<
            RippleAddress::createHumanAccountID (secondAccount) << " and " <<
            RippleAddress::createHumanAccountID (thirdAccount);

        terStatus = terNO_RIPPLE;
    }
}

// Check a fully-expanded path to make sure it doesn't violate no-Ripple settings
void PathState::checkNoRipple (uint160 const& uDstAccountID, uint160 const& uSrcAccountID)
{

    // There must be at least one node for there to be two consecutive ripple lines
    if (vpnNodes.size() == 0)
       return;

    if (vpnNodes.size() == 1)
    {
        // There's just one link in the path
        // We only need to check source-node-dest
        if (is_bit_set (vpnNodes[0].uFlags, STPathElement::typeAccount) &&
            (vpnNodes[0].uAccountID != uSrcAccountID) &&
            (vpnNodes[0].uAccountID != uDstAccountID))
        {
            if (saInReq.getCurrency() != saOutReq.getCurrency())
                terStatus = terNO_LINE;
            else
                checkNoRipple (uSrcAccountID, vpnNodes[0].uAccountID, uDstAccountID,
                    vpnNodes[0].uCurrencyID);
        }
        return;
    }

    // Check source <-> first <-> second
    if (is_bit_set (vpnNodes[0].uFlags, STPathElement::typeAccount) &&
        is_bit_set (vpnNodes[1].uFlags, STPathElement::typeAccount) &&
        (vpnNodes[0].uAccountID != uSrcAccountID))
    {
        if ((vpnNodes[0].uCurrencyID != vpnNodes[1].uCurrencyID))
        {
            terStatus = terNO_LINE;
            return;
        }
        else
        {
            checkNoRipple (uSrcAccountID, vpnNodes[0].uAccountID, vpnNodes[1].uAccountID,
                vpnNodes[0].uCurrencyID);
            if (tesSUCCESS != terStatus)
                return;
        }
    }

    // Check second_from_last <-> last <-> destination
    size_t s = vpnNodes.size() - 2;
    if (is_bit_set (vpnNodes[s].uFlags, STPathElement::typeAccount) &&
        is_bit_set (vpnNodes[s+1].uFlags, STPathElement::typeAccount) &&
        (uDstAccountID != vpnNodes[s+1].uAccountID))
    {
        if ((vpnNodes[s].uCurrencyID != vpnNodes[s+1].uCurrencyID))
        {
            terStatus = terNO_LINE;
            return;
        }
        else
        {
            checkNoRipple (vpnNodes[s].uAccountID, vpnNodes[s+1].uAccountID, uDstAccountID,
                vpnNodes[s].uCurrencyID);
            if (tesSUCCESS != terStatus)
                return;
        }
    }


    // Loop through all nodes that have a prior node and successor nodes
    // These are the nodes whose no ripple constratints could be violated
    for (int i = 1; i < (vpnNodes.size() - 1); ++i)
    {

        if (is_bit_set (vpnNodes[i-1].uFlags, STPathElement::typeAccount) &&
            is_bit_set (vpnNodes[i].uFlags, STPathElement::typeAccount) &&
            is_bit_set (vpnNodes[i+1].uFlags, STPathElement::typeAccount))
        { // two consecutive account-to-account links

            uint160 const& currencyID = vpnNodes[i].uCurrencyID;
            if ((vpnNodes[i-1].uCurrencyID != currencyID) ||
                (vpnNodes[i+1].uCurrencyID != currencyID))
            {
                terStatus = temBAD_PATH;
                return;
            }
            checkNoRipple (
                vpnNodes[i-1].uAccountID, vpnNodes[i].uAccountID, vpnNodes[i+1].uAccountID,
                    currencyID);
            if (terStatus != tesSUCCESS)
                return;
        }

    }
}

// This is for debugging not end users. Output names can be changed without warning.
Json::Value PathState::getJson () const
{
    Json::Value jvPathState (Json::objectValue);
    Json::Value jvNodes (Json::arrayValue);

    BOOST_FOREACH (const Node & pnNode, vpnNodes)
    {
        jvNodes.append (pnNode.getJson ());
    }

    jvPathState["status"]   = terStatus;
    jvPathState["index"]    = mIndex;
    jvPathState["nodes"]    = jvNodes;

    if (saInReq)
        jvPathState["in_req"]   = saInReq.getJson (0);

    if (saInAct)
        jvPathState["in_act"]   = saInAct.getJson (0);

    if (saInPass)
        jvPathState["in_pass"]  = saInPass.getJson (0);

    if (saOutReq)
        jvPathState["out_req"]  = saOutReq.getJson (0);

    if (saOutAct)
        jvPathState["out_act"]  = saOutAct.getJson (0);

    if (saOutPass)
        jvPathState["out_pass"] = saOutPass.getJson (0);

    if (uQuality)
        jvPathState["uQuality"] = boost::lexical_cast<std::string>(uQuality);

    return jvPathState;
}

} // ripple
