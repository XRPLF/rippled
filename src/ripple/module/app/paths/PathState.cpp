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

void PathState::clear() {
    allLiquidityConsumed_ = false;
    saInPass = saInReq.zeroed();
    saOutPass = saOutReq.zeroed();
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

// Make sure last path node delivers to account_: currency_ from issuer_.
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
TER PathState::pushImpliedNodes (
    Account const& account,    // --> Delivering to this account.
    Currency const& currency,  // --> Delivering this currency.
    Account const& issuer)    // --> Delivering this issuer.
{
    TER resultCode = tesSUCCESS;

     WriteLog (lsTRACE, RippleCalc) << "pushImpliedNodes>" <<
         " " << account <<
         " " << currency <<
         " " << issuer;

    if (nodes_.back ().currency_ != currency)
    {
        // Currency is different, need to convert via an offer from an order
        // book.  XRP_ACCOUNT does double duty as signaling "this is an order
        // book".

        // Corresponds to "Implies an offer directory" in the diagram, currently
        // at http://goo.gl/Uj3HAB.

        auto type = isXRP(currency) ? STPathElement::typeCurrency
            : STPathElement::typeCurrency | STPathElement::typeIssuer;

        // The offer's output is what is now wanted.
        // XRP_ACCOUNT is a placeholder for offers.
        resultCode = pushNode (type, XRP_ACCOUNT, currency, issuer);
    }


    // For ripple, non-XRP, ensure the issuer is on at least one side of the
    // transaction.
    if (resultCode == tesSUCCESS
        && !isXRP(currency)
        && nodes_.back ().account_ != issuer
        // Previous is not issuing own IOUs.
        && account != issuer)
        // Current is not receiving own IOUs.
    {
        // Need to ripple through issuer's account.
        // Case "Implies an another node: (pushImpliedNodes)" in the document.
        // Intermediate account is the needed issuer.
        resultCode = pushNode (
            STPathElement::typeAll, issuer, currency, issuer);
    }

    WriteLog (lsTRACE, RippleCalc)
        << "pushImpliedNodes< : " << transToken (resultCode);

    return resultCode;
}

// Append a node, then create and insert before it any implied nodes.  Order
// book nodes may go back to back.
//
// For each non-matching pair of IssuedCurrency, there's an order book.
//
// <-- resultCode: tesSUCCESS, temBAD_PATH, terNO_ACCOUNT, terNO_AUTH,
//                 terNO_LINE, tecPATH_DRY
TER PathState::pushNode (
    const int iType,
    Account const& account,    // If not specified, means an order book.
    Currency const& currency,  // If not specified, default to previous.
    Account const& issuer)     // If not specified, default to previous.
{
    path::Node node;
    const bool pathIsEmpty = nodes_.empty ();

    // TODO(tom): if pathIsEmpty, we probably don't need to do ANYTHING below.
    // Indeed, we might just not even call pushNode in the first place!

    auto const& previousNode = pathIsEmpty ? path::Node () : nodes_.back ();

    // true, iff node is a ripple account. false, iff node is an offer node.
    const bool hasAccount = (iType & STPathElement::typeAccount);

    // Is currency specified for the output of the current node?
    const bool hasCurrency = (iType & STPathElement::typeCurrency);

    // Issuer is specified for the output of the current node.
    const bool hasIssuer = (iType & STPathElement::typeIssuer);

    TER resultCode = tesSUCCESS;

    WriteLog (lsTRACE, RippleCalc)
         << "pushNode> " << iType << ": "
         << (hasAccount ? to_string(account) : std::string("-")) << " "
         << (hasCurrency ? to_string(currency) : std::string("-")) << "/"
         << (hasIssuer ? to_string(issuer) : std::string("-")) << "/";

    node.uFlags = iType;
    node.currency_ = hasCurrency ? currency : Currency(previousNode.currency_);

    // TODO(tom): we can probably just return immediately whenever we hit an
    // error in these next pages.

    if (iType & ~STPathElement::typeAll)
    {
        // Of course, this could never happen.
        WriteLog (lsDEBUG, RippleCalc) << "pushNode: bad bits.";
        resultCode   = temBAD_PATH;
    }
    else if (hasIssuer && !node.currency_)
    {
        WriteLog (lsDEBUG, RippleCalc) << "pushNode: issuer specified for XRP.";

        resultCode   = temBAD_PATH;
    }
    else if (hasIssuer && !issuer)
    {
        WriteLog (lsDEBUG, RippleCalc) << "pushNode: specified bad issuer.";

        resultCode   = temBAD_PATH;
    }
    else if (!hasAccount && !hasCurrency && !hasIssuer)
    {
        // You can't default everything to the previous node as you would make
        // no progress.
        WriteLog (lsDEBUG, RippleCalc)
            << "pushNode: offer must specify at least currency or issuer.";
        resultCode   = temBAD_PATH;
    }
    else if (hasAccount)
    {
        // Account link
        node.account_    = account;
        node.issuer_     = hasIssuer
                              ? issuer
                : !!node.currency_  // Not XRP.
                              ? account
                              : XRP_ACCOUNT;
        // Zero value - for accounts.
        node.saRevRedeem   = STAmount (node.currency_, account);
        node.saRevIssue    = node.saRevRedeem;

        // For order books only - zero currency with the issuer ID.
        node.saRevDeliver  = STAmount (node.currency_, node.issuer_);
        node.saFwdDeliver  = node.saRevDeliver;

        if (pathIsEmpty)
        {
            // The first node is always correct as is.
        }
        else if (!account)
        {
            WriteLog (lsDEBUG, RippleCalc)
                << "pushNode: specified bad account.";
            resultCode   = temBAD_PATH;
        }
        else
        {
            // Add required intermediate nodes to deliver to current account.
            WriteLog (lsTRACE, RippleCalc)
                << "pushNode: imply for account.";

            resultCode = pushImpliedNodes (
                node.account_, node.currency_,
                isXRP(node.currency_) ? XRP_ACCOUNT : account);

            // Note: previousNode may no longer be the immediately previous node.
        }

        if (resultCode == tesSUCCESS && !nodes_.empty ())
        {
            auto const& backNode = nodes_.back ();
            if (backNode.isAccount())
            {
                auto sleRippleState = lesEntries.entryCache (
                    ltRIPPLE_STATE,
                    Ledger::getRippleStateIndex (
                        backNode.account_, node.account_, backNode.currency_));

                // A "RippleState" means a balance betweeen two accounts for a
                // specific currency.
                if (!sleRippleState)
                {
                    WriteLog (lsTRACE, RippleCalc)
                            << "pushNode: No credit line between "
                            << backNode.account_ << " and " << node.account_
                            << " for " << node.currency_ << "." ;

                    WriteLog (lsTRACE, RippleCalc) << getJson ();

                    resultCode   = terNO_LINE;
                }
                else
                {
                    WriteLog (lsTRACE, RippleCalc)
                            << "pushNode: Credit line found between "
                            << backNode.account_ << " and " << node.account_
                            << " for " << node.currency_ << "." ;

                    auto sleBck  = lesEntries.entryCache (
                        ltACCOUNT_ROOT,
                        Ledger::getAccountRootIndex (backNode.account_));
                    // Is the source account the highest numbered account ID?
                    bool bHigh = backNode.account_ > node.account_;

                    if (!sleBck)
                    {
                        WriteLog (lsWARNING, RippleCalc)
                            << "pushNode: delay: can't receive IOUs from "
                            << "non-existent issuer: " << backNode.account_;

                        resultCode   = terNO_ACCOUNT;
                    }
                    else if ((sleBck->getFieldU32 (sfFlags) & lsfRequireAuth) &&
                             !(sleRippleState->getFieldU32 (sfFlags) &
                                  (bHigh ? lsfHighAuth : lsfLowAuth)) &&
                             sleRippleState->getFieldAmount(sfBalance) == zero)
                    {
                        WriteLog (lsWARNING, RippleCalc)
                                << "pushNode: delay: can't receive IOUs from "
                                << "issuer without auth.";

                        resultCode   = terNO_AUTH;
                    }

                    if (resultCode == tesSUCCESS)
                    {
                        STAmount saOwed = lesEntries.rippleOwed (
                            node.account_, backNode.account_, node.currency_);
                        STAmount saLimit;

                        if (saOwed <= zero) {
                            saLimit = lesEntries.rippleLimit (
                                node.account_, backNode.account_,
                                node.currency_);
                            if (-saOwed >= saLimit)
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
        }

        if (resultCode == tesSUCCESS)
            nodes_.push_back (node);
    }
    else
    {
        // Offer link.
        //
        // Offers bridge a change in currency and issuer, or just a change in
        // issuer.
        node.issuer_ = hasIssuer
            ? issuer
            : !!node.currency_
            ? !!previousNode.issuer_
            ? Account(previousNode.issuer_)   // Default to previous issuer
            : Account(previousNode.account_)
            // Or previous account if no previous issuer.
                  : XRP_ACCOUNT;
        node.saRateMax = saZero;
        node.saRevDeliver = STAmount (node.currency_, node.issuer_);
        node.saFwdDeliver = node.saRevDeliver;

        if (node.currency_.isZero() != node.issuer_.isZero())
        {
            WriteLog (lsDEBUG, RippleCalc)
                << "pushNode: currency is inconsistent with issuer.";

            resultCode = temBAD_PATH;
        }
        else if (previousNode.currency_ == node.currency_ &&
                 previousNode.issuer_ == node.issuer_)
        {
            WriteLog (lsDEBUG, RippleCalc) <<
                "pushNode: bad path: offer to same currency and issuer";
            resultCode = temBAD_PATH;
        } else {
            WriteLog (lsTRACE, RippleCalc) << "pushNode: imply for offer.";

            // Insert intermediary issuer account if needed.
            resultCode   = pushImpliedNodes (
                XRP_ACCOUNT, // Rippling, but offers don't have an account.
                previousNode.currency_,
                previousNode.issuer_);
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
// terStatus = tesSUCCESS, temBAD_PATH, terNO_LINE, terNO_ACCOUNT, terNO_AUTH,
// or temBAD_PATH_LOOP
void PathState::expandPath (
    const LedgerEntrySet& lesSource,
    const STPath& spSourcePath,
    Account const& uReceiverID,
    Account const& uSenderID)
{
    uQuality = 1;            // Mark path as active.

    const Currency uMaxCurrencyID = saInReq.getCurrency ();
    const Account uMaxIssuerID = saInReq.getIssuer ();

    const Currency uOutCurrencyID = saOutReq.getCurrency ();
    const Account uOutIssuerID = saOutReq.getIssuer ();
    const Account uSenderIssuerID
        = isXRP(uMaxCurrencyID) ? XRP_ACCOUNT : uSenderID;
    // Sender is always issuer for non-XRP.

    WriteLog (lsTRACE, RippleCalc)
        << "expandPath> " << spSourcePath.getJson (0);

    lesEntries = lesSource.duplicate ();

    terStatus = tesSUCCESS;

    // XRP with issuer is malformed.
    if ((!uMaxCurrencyID && !!uMaxIssuerID)
        || (!uOutCurrencyID && !!uOutIssuerID))
    {
        terStatus   = temBAD_PATH;
    }

    // Push sending node.
    // For non-XRP, issuer is always sending account.
    // - Trying to expand, not-compact.
    // - Every issuer will be traversed through.
    if (terStatus == tesSUCCESS)
    {
        terStatus   = pushNode (
            !isXRP(uMaxCurrencyID)
            ? STPathElement::typeAccount | STPathElement::typeCurrency |
              STPathElement::typeIssuer
            : STPathElement::typeAccount | STPathElement::typeCurrency,
            uSenderID,
            uMaxCurrencyID, // Max specifies the currency.
            uSenderIssuerID);
    }

    WriteLog (lsDEBUG, RippleCalc)
        << "expandPath: pushed:"
        << " account=" << uSenderID
        << " currency=" << uMaxCurrencyID
        << " issuer=" << uSenderIssuerID;

    if (tesSUCCESS == terStatus
            && uMaxIssuerID != uSenderIssuerID)
        // Issuer was not same as sender.
    {
        // May have an implied account node.
        // - If it was XRP, then issuers would have matched.

        // Figure out next node properties for implied node.
        const auto uNxtCurrencyID  = spSourcePath.size ()
                ? Currency(spSourcePath.getElement (0).getCurrency ())
                // Use next node.
                : uOutCurrencyID;
                // Use send.

        // TODO(tom): complexify this next logic further in case someone
        // understands it.
        const auto nextAccountID   = spSourcePath.size ()
                ? Account(spSourcePath.getElement (0).getAccountID ())
                : !isXRP(uOutCurrencyID)
                ? (uOutIssuerID == uReceiverID)
                ? Account(uReceiverID)
                : Account(uOutIssuerID)                      // Use implied node.
                : XRP_ACCOUNT;

        WriteLog (lsDEBUG, RippleCalc)
            << "expandPath: implied check:"
            << " uMaxIssuerID=" << uMaxIssuerID
            << " uSenderIssuerID=" << uSenderIssuerID
            << " uNxtCurrencyID=" << uNxtCurrencyID
            << " nextAccountID=" << nextAccountID;

        // Can't just use push implied, because it can't compensate for next
        // account.
        if (!uNxtCurrencyID
            // Next is XRP, offer next. Must go through issuer.
            || uMaxCurrencyID != uNxtCurrencyID
            // Next is different currency, offer next...
            || uMaxIssuerID != nextAccountID)
            // Next is not implied issuer
        {
            WriteLog (lsDEBUG, RippleCalc)
                << "expandPath: sender implied:"
                << " account=" << uMaxIssuerID
                << " currency=" << uMaxCurrencyID
                << " issuer=" << uMaxIssuerID;

            // Add account implied by SendMax.
            terStatus = pushNode (
                !isXRP(uMaxCurrencyID)
                    ? STPathElement::typeAccount | STPathElement::typeCurrency |
                      STPathElement::typeIssuer
                    : STPathElement::typeAccount | STPathElement::typeCurrency,
                uMaxIssuerID,
                uMaxCurrencyID,
                uMaxIssuerID);
        }
    }

    for (auto & speElement: spSourcePath)
    {
        if (terStatus == tesSUCCESS)
        {
            WriteLog (lsTRACE, RippleCalc) << "expandPath: element in path";
            terStatus = pushNode (
                speElement.getNodeType (), speElement.getAccountID (),
                speElement.getCurrency (), speElement.getIssuerID ());
        }
    }

    auto const& previousNode = nodes_.back ();

    if (terStatus == tesSUCCESS
        && !isXRP(uOutCurrencyID)                         // Next is not XRP
        && uOutIssuerID != uReceiverID              // Out issuer is not receiver
        && (previousNode.currency_ != uOutCurrencyID
        // Previous will be an offer.
            || previousNode.account_ != uOutIssuerID))
        // Need the implied issuer.
    {
        // Add implied account.
        WriteLog (lsDEBUG, RippleCalc)
            << "expandPath: receiver implied:"
            << " account=" << uOutIssuerID
            << " currency=" << uOutCurrencyID
            << " issuer=" << uOutIssuerID;

        terStatus   = pushNode (
            !isXRP(uOutCurrencyID)
                ? STPathElement::typeAccount | STPathElement::typeCurrency |
                  STPathElement::typeIssuer
                : STPathElement::typeAccount | STPathElement::typeCurrency,
            uOutIssuerID,
            uOutCurrencyID,
            uOutIssuerID);
    }

    if (terStatus == tesSUCCESS)
    {
        // Create receiver node.
        // Last node is always an account.

        terStatus   = pushNode (
            !isXRP(uOutCurrencyID)
                ? STPathElement::typeAccount | STPathElement::typeCurrency |
                   STPathElement::typeIssuer
               : STPathElement::typeAccount | STPathElement::typeCurrency,
            uReceiverID,                                    // Receive to output
            uOutCurrencyID,                                 // Desired currency
            uReceiverID);
    }

    if (terStatus == tesSUCCESS)
    {
        // Look for first mention of source in nodes and detect loops.
        // Note: The output is not allowed to be a source.

        const unsigned int  uNodes  = nodes_.size ();

        for (unsigned int nodeIndex = 0;
             tesSUCCESS == terStatus && nodeIndex != uNodes; ++nodeIndex)
        {
            const auto&  node   = nodes_[nodeIndex];

            AccountCurrencyIssuer aci(
                node.account_, node.currency_, node.issuer_);
            if (!umForward.insert (std::make_pair (aci, nodeIndex)).second)
            {
                // Failed to insert. Have a loop.
                WriteLog (lsDEBUG, RippleCalc) <<
                    "expandPath: loop detected: " << getJson ();

                terStatus   = temBAD_PATH_LOOP;
            }
        }
    }

    WriteLog (lsDEBUG, RippleCalc)
        << "expandPath:"
        << " in=" << uMaxCurrencyID
        << "/" << uMaxIssuerID
        << " out=" << uOutCurrencyID
        << "/" << uOutIssuerID
        << ": " << getJson ();
}

/** Check if a sequence of three accounts violates the no ripple constrains
    [first] -> [second] -> [third]
    Disallowed if 'second' set no ripple on [first]->[second] and
    [second]->[third]
*/
void PathState::checkNoRipple (
    Account const& firstAccount,
    Account const& secondAccount,
    // This is the account whose constraints we are checking
    Account const& thirdAccount,
    Currency const& currency)
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
        WriteLog (lsINFO, RippleCalc)
            << "Path violates noRipple constraint between "
            << firstAccount << ", "
            << secondAccount << " and "
            << thirdAccount;

        terStatus = terNO_RIPPLE;
    }
}

// Check a fully-expanded path to make sure it doesn't violate no-Ripple
// settings.
void PathState::checkNoRipple (
    Account const& uDstAccountID, Account const& uSrcAccountID)
{
    // There must be at least one node for there to be two consecutive ripple
    // lines.
    if (nodes_.size() == 0)
       return;

    if (nodes_.size() == 1)
    {
        // There's just one link in the path
        // We only need to check source-node-dest
        if (nodes_[0].isAccount() &&
            (nodes_[0].account_ != uSrcAccountID) &&
            (nodes_[0].account_ != uDstAccountID))
        {
            if (saInReq.getCurrency() != saOutReq.getCurrency())
                terStatus = terNO_LINE;
            else
                checkNoRipple (uSrcAccountID, nodes_[0].account_, uDstAccountID,
                               nodes_[0].currency_);
        }
        return;
    }

    // Check source <-> first <-> second
    if (nodes_[0].isAccount() &&
        nodes_[1].isAccount() &&
        (nodes_[0].account_ != uSrcAccountID))
    {
        if ((nodes_[0].currency_ != nodes_[1].currency_))
        {
            terStatus = terNO_LINE;
            return;
        }
        else
        {
            checkNoRipple (uSrcAccountID, nodes_[0].account_, nodes_[1].account_,
                nodes_[0].currency_);
            if (tesSUCCESS != terStatus)
                return;
        }
    }

    // Check second_from_last <-> last <-> destination
    size_t s = nodes_.size() - 2;
    if (nodes_[s].isAccount() &&
        nodes_[s + 1].isAccount() &&
        (uDstAccountID != nodes_[s+1].account_))
    {
        if ((nodes_[s].currency_ != nodes_[s+1].currency_))
        {
            terStatus = terNO_LINE;
            return;
        }
        else
        {
            checkNoRipple (nodes_[s].account_, nodes_[s+1].account_,
                           uDstAccountID, nodes_[s].currency_);
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

            uint160 const& currencyID = nodes_[i].currency_;
            if ((nodes_[i-1].currency_ != currencyID) ||
                (nodes_[i+1].currency_ != currencyID))
            {
                terStatus = temBAD_PATH;
                return;
            }
            checkNoRipple (
                nodes_[i-1].account_, nodes_[i].account_, nodes_[i+1].account_,
                currencyID);
            if (terStatus != tesSUCCESS)
                return;
        }

    }
}

// This is for debugging not end users. Output names can be changed without
// warning.
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
