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

// OPTIMIZE: When calculating path increment, note if increment consumes all
// liquidity. No need to revisit path in the future if all liquidity is used.
//

class RippleCalc; // for logging

std::size_t hash_value (const AccountCurrencyIssuer& asValue)
{
    std::size_t const seed = 0;
    return beast::hardened_hash<AccountCurrencyIssuer>{seed}(asValue);
}

void PathState::clear() {
    allLiquidityConsumed_ = false;
    saInPass = zeroed (saInReq);
    saOutPass = zeroed (saOutReq);
    vUnfundedBecame.clear ();
    umReverse.clear ();
}

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
// If the unadded next node as specified by arguments would not work as is, then
// add the necessary nodes so it would work.
// PRECONDITION: the PathState must be non-empty.
//
// Rules:
// - Currencies must be converted via an offer.
// - A node names its output.

// - A ripple nodes output issuer must be the node's account or the next node's
//   account.
// - Offers can only go directly to another offer if the currency and issuer are
//   an exact match.
// - Real issuers must be specified for non-XRP.
TER PathState::pushImply (
    const uint160& uAccountID,  // --> Delivering to this account.
    const uint160& uCurrencyID, // --> Delivering this currency.
    const uint160& uIssuerID)   // --> Delivering this issuer.
{
    auto const& previousNode       = nodes_.back ();
    TER resultCode   = tesSUCCESS;

     WriteLog (lsTRACE, RippleCalc) << "pushImply>" <<
        " " << RippleAddress::createHumanAccountID (uAccountID) <<
        " " << STAmount::createHumanCurrency (uCurrencyID) <<
        " " << RippleAddress::createHumanAccountID (uIssuerID);

    if (previousNode.uCurrencyID != uCurrencyID)
    {
        // Currency is different, need to convert via an offer.
        // Currency is different, need to convert via an offer from an order
        // book.  ACCOUNT_XRP does double duty as signaling "this is an order
        // book".

        // Corresponds to "Implies an offer directory" in the diagram, currently
        // at https://docs.google.com/a/ripple.com/document/d/1b1RC8pKIgVZqUmjf9MW4IYxvzU7cBla4-pCSBbV4u8Q/edit

        resultCode   = pushNode ( // Offer.
                          !!uCurrencyID
                          ? STPathElement::typeCurrency | STPathElement::typeIssuer
                          : STPathElement::typeCurrency,
                          ACCOUNT_XRP,                    // Placeholder for offers.
                          uCurrencyID,                    // The offer's output is what is now wanted.
                          uIssuerID);
    }

    auto const& pnBck = nodes_.back ();

    // For ripple, non-XRP, ensure the issuer is on at least one side of the transaction.
    if (resultCode == tesSUCCESS
        && !!uCurrencyID                                // Not XRP.
        && (pnBck.uAccountID != uIssuerID               // Previous is not issuing own IOUs.
            && uAccountID != uIssuerID))                // Current is not receiving own IOUs.
    {
        // Need to ripple through uIssuerID's account.
        // Case "Implies an another node: (pushImply)" in the document.
        resultCode   = pushNode (
                          STPathElement::typeAccount | STPathElement::typeCurrency | STPathElement::typeIssuer,
                          uIssuerID,                      // Intermediate account is the needed issuer.
                          uCurrencyID,
                          uIssuerID);
    }

    WriteLog (lsTRACE, RippleCalc) << "pushImply< : " << transToken (resultCode);

    return resultCode;
}

// Append a node, then create and insert before it any implied nodes.  Order
// book nodes may go back to back.
//
// For each non-matching pair of IssuedCurrency, there's an order book.
//
// <-- resultCode: tesSUCCESS, temBAD_PATH, terNO_ACCOUNT, terNO_AUTH, terNO_LINE, tecPATH_DRY
TER PathState::pushNode (
    const int iType,
    const uint160& uAccountID,   // If not specified, means an order book.
    const uint160& uCurrencyID,  // If not specified, default to previous.
    const uint160& uIssuerID)    // If not specified, default to previous.
{
    path::Node node;
    const bool bFirst      = nodes_.empty ();
    auto const& previousNode = bFirst ? path::Node () : nodes_.back ();

    // true, iff node is a ripple account. false, iff node is an offer node.
    const bool bAccount (iType & STPathElement::typeAccount);

    // Is currency specified for the output of the current node?
    const bool bCurrency (iType & STPathElement::typeCurrency);

    // Issuer is specified for the output of the current node.
    const bool bIssuer (iType & STPathElement::typeIssuer);

    TER resultCode   = tesSUCCESS;

    WriteLog (lsTRACE, RippleCalc) << "pushNode> " <<
       iType <<
       ": " << (bAccount ? RippleAddress::createHumanAccountID (uAccountID) : "-") <<
       " " << (bCurrency ? STAmount::createHumanCurrency (uCurrencyID) : "-") <<
       "/" << (bIssuer ? RippleAddress::createHumanAccountID (uIssuerID) : "-");

    node.uFlags = iType;
    node.uCurrencyID = bCurrency ? uCurrencyID : previousNode.uCurrencyID;

    if (iType & ~STPathElement::typeValidBits)
    {
        WriteLog (lsDEBUG, RippleCalc) << "pushNode: bad bits.";

        resultCode   = temBAD_PATH;
    }
    else if (bIssuer && !node.uCurrencyID)
    {
        WriteLog (lsDEBUG, RippleCalc) << "pushNode: issuer specified for XRP.";

        resultCode   = temBAD_PATH;
    }
    else if (bIssuer && !uIssuerID)
    {
        WriteLog (lsDEBUG, RippleCalc) << "pushNode: specified bad issuer.";

        resultCode   = temBAD_PATH;
    }
    else if (!bAccount && !bCurrency && !bIssuer)
    {
        // You can't default everything to the previous node as you would make
        // no progress.
        WriteLog (lsDEBUG, RippleCalc) << "pushNode: offer must specify at least currency or issuer.";

        resultCode   = temBAD_PATH;
    }
    else if (bAccount)
    {
        // Account link

        node.uAccountID    = uAccountID;
        node.uIssuerID     = bIssuer
                              ? uIssuerID
                : !!node.uCurrencyID  // Not XRP.
                              ? uAccountID
                              : ACCOUNT_XRP;
        // Zero value - for accounts.
        node.saRevRedeem   = STAmount (node.uCurrencyID, uAccountID);
        node.saRevIssue    = node.saRevRedeem;

        // For order books only - zero currency with the issuer ID.
        node.saRevDeliver  = STAmount (node.uCurrencyID, node.uIssuerID);
        node.saFwdDeliver  = node.saRevDeliver;

        if (bFirst)
        {
            // The first node is always correct as is.

            nothing ();
        }
        else if (!uAccountID)
        {
            WriteLog (lsDEBUG, RippleCalc) << "pushNode: specified bad account.";

            resultCode   = temBAD_PATH;
        }
        else
        {
            // Add required intermediate nodes to deliver to current account.
            WriteLog (lsTRACE, RippleCalc) << "pushNode: imply for account.";

            resultCode   = pushImply (
                              node.uAccountID,                                   // Current account.
                              node.uCurrencyID,                                  // Wanted currency.
                              !!node.uCurrencyID ? uAccountID : ACCOUNT_XRP);    // Account as wanted issuer.

            // Note: previousNode may no longer be the immediately previous node.
        }

        if (resultCode == tesSUCCESS && !nodes_.empty ())
        {
            auto const& pnBck = nodes_.back ();
            bool bBckAccount = pnBck.isAccount();

            if (bBckAccount)
            {
                SLE::pointer    sleRippleState  = lesEntries.entryCache (ltRIPPLE_STATE, Ledger::getRippleStateIndex (pnBck.uAccountID, node.uAccountID, pnBck.uCurrencyID));

                // A "RippleState" means a balance betweeen two accounts for a
                // specific currency.
                if (!sleRippleState)
                {
                    WriteLog (lsTRACE, RippleCalc) << "pushNode: No credit line between "
                                                   << RippleAddress::createHumanAccountID (pnBck.uAccountID)
                                                   << " and "
                                                   << RippleAddress::createHumanAccountID (node.uAccountID)
                                                   << " for "
                                                   << STAmount::createHumanCurrency (node.uCurrencyID)
                                                   << "." ;

                    WriteLog (lsTRACE, RippleCalc) << getJson ();

                    resultCode   = terNO_LINE;
                }
                else
                {
                    WriteLog (lsTRACE, RippleCalc) << "pushNode: Credit line found between "
                                                   << RippleAddress::createHumanAccountID (pnBck.uAccountID)
                                                   << " and "
                                                   << RippleAddress::createHumanAccountID (node.uAccountID)
                                                   << " for "
                                                   << STAmount::createHumanCurrency (node.uCurrencyID)
                                                   << "." ;

                    SLE::pointer        sleBck  = lesEntries.entryCache (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (pnBck.uAccountID));
                    // Is the source account the highest numbered account ID?
                    bool                bHigh   = pnBck.uAccountID > node.uAccountID;

                    if (!sleBck)
                    {
                        WriteLog (lsWARNING, RippleCalc) << "pushNode: delay: can't receive IOUs from non-existent issuer: " << RippleAddress::createHumanAccountID (pnBck.uAccountID);

                        resultCode   = terNO_ACCOUNT;
                    }
                    else if (sleBck->getFieldU32 (sfFlags) & lsfRequireAuth
                             && !(sleRippleState->getFieldU32 (sfFlags) & (bHigh ? lsfHighAuth : lsfLowAuth))
                             && sleRippleState->getFieldAmount(sfBalance) == zero) // CHECKME
                    {
                        WriteLog (lsWARNING, RippleCalc) << "pushNode: delay: can't receive IOUs from issuer without auth.";

                        resultCode   = terNO_AUTH;
                    }

                    if (resultCode == tesSUCCESS)
                    {
                        STAmount    saOwed  = lesEntries.rippleOwed (node.uAccountID, pnBck.uAccountID, node.uCurrencyID);
                        STAmount    saLimit;

                        if (saOwed <= zero
                                && -saOwed >= (saLimit = lesEntries.rippleLimit (node.uAccountID, pnBck.uAccountID, node.uCurrencyID)))
                        {
                            WriteLog (lsWARNING, RippleCalc) <<
                                "pushNode: dry:" <<
                                " saOwed=" << saOwed <<
                                " saLimit=" << saLimit;

                            resultCode   = tecPATH_DRY;
                        }
                    }
                }
            }
        }

        if (resultCode == tesSUCCESS)
        {
            nodes_.push_back (node);
        }
    }
    else
    {
        // Offer link
        // Offers bridge a change in currency & issuer or just a change in issuer.
        node.uIssuerID     = bIssuer
                              ? uIssuerID
                              : !!node.uCurrencyID
                              ? !!previousNode.uIssuerID
                              ? previousNode.uIssuerID   // Default to previous issuer
                              : previousNode.uAccountID  // Or previous account if no previous issuer.
                      : ACCOUNT_XRP;
        node.saRateMax     = saZero;
        node.saRevDeliver  = STAmount (node.uCurrencyID, node.uIssuerID);
        node.saFwdDeliver  = node.saRevDeliver;

        if (node.uCurrencyID.isZero() != node.uIssuerID.isZero())
        {
            WriteLog (lsDEBUG, RippleCalc)
                << "pushNode: currency is inconsistent with issuer.";

            resultCode = temBAD_PATH;
        }
        else if (previousNode.uCurrencyID == node.uCurrencyID &&
                 previousNode.uIssuerID == node.uIssuerID)
        {
            WriteLog (lsDEBUG, RippleCalc) <<
                "pushNode: bad path: offer to same currency and issuer";
            resultCode = temBAD_PATH;
        } else {
            WriteLog (lsTRACE, RippleCalc) << "pushNode: imply for offer.";

            // Insert intermediary issuer account if needed.
            resultCode   = pushImply (
                ACCOUNT_XRP, // Rippling, but offers don't have an account.
                previousNode.uCurrencyID,
                previousNode.uIssuerID);
        }

        if (resultCode == tesSUCCESS)
        {
            nodes_.push_back (node);
        }
    }

    WriteLog (lsTRACE, RippleCalc) << "pushNode< : " << transToken (resultCode);
    return resultCode;
}

// Set this object to be an expanded path from spSourcePath - take the implied
// nodes and makes them explicit.  It also sanitizes the path.
//
// There are only two types of nodes: account nodes and order books nodes.
//
// You can infer some nodes automatically.  If you're paying me bitstamp USD,
// then there must be an intermediate bitstamp node.
//
// If you have accounts A and B, and they're delivery currency issued by C, then
// there must be a node with account C in the middle.
//
// If you're paying USD and getting bitcoins, there has to be an order book in
// between.
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
        const uint160   nextAccountID   = spSourcePath.size ()
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
            " nextAccountID=" << RippleAddress::createHumanAccountID (nextAccountID);

        // Can't just use push implied, because it can't compensate for next account.
        if (!uNxtCurrencyID                         // Next is XRP, offer next. Must go through issuer.
                || uMaxCurrencyID != uNxtCurrencyID // Next is different currency, offer next...
                || uMaxIssuerID != nextAccountID)   // Next is not implied issuer
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

    auto const&  previousNode = nodes_.back ();

    if (tesSUCCESS == terStatus
            && !!uOutCurrencyID                         // Next is not XRP
            && uOutIssuerID != uReceiverID              // Out issuer is not receiver
            && (previousNode.uCurrencyID != uOutCurrencyID     // Previous will be an offer.
                || previousNode.uAccountID != uOutIssuerID))   // Need the implied issuer.
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

        const unsigned int  uNodes  = nodes_.size ();

        for (unsigned int nodeIndex = 0; tesSUCCESS == terStatus && nodeIndex != uNodes; ++nodeIndex)
        {
            const auto&  node   = nodes_[nodeIndex];

            AccountCurrencyIssuer aci(
                node.uAccountID, node.uCurrencyID, node.uIssuerID);
            if (!umForward.insert (std::make_pair (aci, nodeIndex)).second)
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
        sleIn->getFieldU32 (sfFlags) &
            ((secondAccount > firstAccount) ? lsfHighNoRipple : lsfLowNoRipple) &&
        sleOut->getFieldU32 (sfFlags) &
            ((secondAccount > thirdAccount) ? lsfHighNoRipple : lsfLowNoRipple))
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
    if (nodes_.size() == 0)
       return;

    if (nodes_.size() == 1)
    {
        // There's just one link in the path
        // We only need to check source-node-dest
        if (nodes_[0].isAccount() &&
            (nodes_[0].uAccountID != uSrcAccountID) &&
            (nodes_[0].uAccountID != uDstAccountID))
        {
            if (saInReq.getCurrency() != saOutReq.getCurrency())
                terStatus = terNO_LINE;
            else
                checkNoRipple (uSrcAccountID, nodes_[0].uAccountID, uDstAccountID,
                    nodes_[0].uCurrencyID);
        }
        return;
    }

    // Check source <-> first <-> second
    if (nodes_[0].isAccount() &&
        nodes_[1].isAccount() &&
        (nodes_[0].uAccountID != uSrcAccountID))
    {
        if ((nodes_[0].uCurrencyID != nodes_[1].uCurrencyID))
        {
            terStatus = terNO_LINE;
            return;
        }
        else
        {
            checkNoRipple (uSrcAccountID, nodes_[0].uAccountID, nodes_[1].uAccountID,
                nodes_[0].uCurrencyID);
            if (tesSUCCESS != terStatus)
                return;
        }
    }

    // Check second_from_last <-> last <-> destination
    size_t s = nodes_.size() - 2;
    if (nodes_[s].isAccount() &&
        nodes_[s + 1].isAccount() &&
        (uDstAccountID != nodes_[s+1].uAccountID))
    {
        if ((nodes_[s].uCurrencyID != nodes_[s+1].uCurrencyID))
        {
            terStatus = terNO_LINE;
            return;
        }
        else
        {
            checkNoRipple (nodes_[s].uAccountID, nodes_[s+1].uAccountID, uDstAccountID,
                nodes_[s].uCurrencyID);
            if (tesSUCCESS != terStatus)
                return;
        }
    }

    // Loop through all nodes that have a prior node and successor nodes
    // These are the nodes whose no ripple constratints could be violated
    for (int i = 1; i < nodes_.size() - 1; ++i)
    {
        if (nodes_[i - 1].isAccount() &&
            nodes_[i].isAccount() &&
            nodes_[i + 1].isAccount())
        { // Two consecutive account-to-account links

            uint160 const& currencyID = nodes_[i].uCurrencyID;
            if ((nodes_[i-1].uCurrencyID != currencyID) ||
                (nodes_[i+1].uCurrencyID != currencyID))
            {
                terStatus = temBAD_PATH;
                return;
            }
            checkNoRipple (
                nodes_[i-1].uAccountID, nodes_[i].uAccountID, nodes_[i+1].uAccountID,
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

    for (auto const &pnNode: nodes_)
        jvNodes.append (pnNode.getJson ());

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
