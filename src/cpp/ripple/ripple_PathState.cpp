//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

// TODO:
// - Do automatic bridging via XRP.
//
// OPTIMIZE: When calculating path increment, note if increment consumes all liquidity. No need to revisit path in the future if
// all liquidity is used.
//

class RippleCalc; // for logging

std::size_t hash_value (const aciSource& asValue)
{
    std::size_t seed = 0;

    asValue.get<0> ().hash_combine (seed);
    asValue.get<1> ().hash_combine (seed);
    asValue.get<2> ().hash_combine (seed);

    return seed;
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

    if (isSetBit (uFlags, STPathElement::typeAccount) || !!uAccountID)
        jvFlags.append (!!isSetBit (uFlags, STPathElement::typeAccount) == !!uAccountID ? "account" : "-account");

    if (isSetBit (uFlags, STPathElement::typeCurrency) || !!uCurrencyID)
        jvFlags.append (!!isSetBit (uFlags, STPathElement::typeCurrency) == !!uCurrencyID ? "currency" : "-currency");

    if (isSetBit (uFlags, STPathElement::typeIssuer) || !!uIssuerID)
        jvFlags.append (!!isSetBit (uFlags, STPathElement::typeIssuer) == !!uIssuerID ? "issuer" : "-issuer");

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
    TER                 terResult   = tesSUCCESS;

    WriteLog (lsTRACE, RippleCalc) << "pushImply> "
                                   << RippleAddress::createHumanAccountID (uAccountID)
                                   << " " << STAmount::createHumanCurrency (uCurrencyID)
                                   << " " << RippleAddress::createHumanAccountID (uIssuerID);

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

    WriteLog (lsTRACE, RippleCalc) << boost::str (boost::format ("pushImply< : %s") % transToken (terResult));

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
    Node         pnCur;
    const bool          bFirst      = vpnNodes.empty ();
    const Node&  pnPrv       = bFirst ? Node () : vpnNodes.back ();
    // true, iff node is a ripple account. false, iff node is an offer node.
    const bool          bAccount    = isSetBit (iType, STPathElement::typeAccount);
    // true, iff currency supplied.
    // Currency is specified for the output of the current node.
    const bool          bCurrency   = isSetBit (iType, STPathElement::typeCurrency);
    // Issuer is specified for the output of the current node.
    const bool          bIssuer     = isSetBit (iType, STPathElement::typeIssuer);
    TER                 terResult   = tesSUCCESS;

    WriteLog (lsTRACE, RippleCalc) << "pushNode> "
                                   << iType
                                   << ": " << (bAccount ? RippleAddress::createHumanAccountID (uAccountID) : "-")
                                   << " " << (bCurrency ? STAmount::createHumanCurrency (uCurrencyID) : "-")
                                   << "/" << (bIssuer ? RippleAddress::createHumanAccountID (uIssuerID) : "-");

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
            const Node&  pnBck       = vpnNodes.back ();
            bool                bBckAccount = isSetBit (pnBck.uFlags, STPathElement::typeAccount);

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
                    else if (isSetBit (sleBck->getFieldU32 (sfFlags), lsfRequireAuth)
                             && !isSetBit (sleRippleState->getFieldU32 (sfFlags), (bHigh ? lsfHighAuth : lsfLowAuth)))
                    {
                        WriteLog (lsWARNING, RippleCalc) << "pushNode: delay: can't receive IOUs from issuer without auth.";

                        terResult   = terNO_AUTH;
                    }

                    if (tesSUCCESS == terResult)
                    {
                        STAmount    saOwed  = lesEntries.rippleOwed (pnCur.uAccountID, pnBck.uAccountID, pnCur.uCurrencyID);
                        STAmount    saLimit;

                        if (!saOwed.isPositive ()
                                && -saOwed >= (saLimit = lesEntries.rippleLimit (pnCur.uAccountID, pnBck.uAccountID, pnCur.uCurrencyID)))
                        {
                            WriteLog (lsWARNING, RippleCalc) << boost::str (boost::format ("pushNode: dry: saOwed=%s saLimit=%s")
                                                             % saOwed
                                                             % saLimit);

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
                              ACCOUNT_XRP,                // Rippling, but offers don't have an account.
                              pnPrv.uCurrencyID,
                              pnPrv.uIssuerID);
        }

        if (tesSUCCESS == terResult)
        {
            vpnNodes.push_back (pnCur);
        }
    }

    WriteLog (lsTRACE, RippleCalc) << boost::str (boost::format ("pushNode< : %s") % transToken (terResult));

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

    WriteLog (lsDEBUG, RippleCalc) << boost::str (boost::format ("setExpanded> %s") % spSourcePath.getJson (0));

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
                          uMaxCurrencyID,                                 // Max specifes the currency.
                          uSenderIssuerID);

    WriteLog (lsDEBUG, RippleCalc) << boost::str (boost::format ("setExpanded: pushed: account=%s currency=%s issuer=%s")
                                   % RippleAddress::createHumanAccountID (uSenderID)
                                   % STAmount::createHumanCurrency (uMaxCurrencyID)
                                   % RippleAddress::createHumanAccountID (uSenderIssuerID));

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

        WriteLog (lsDEBUG, RippleCalc) << boost::str (boost::format ("setExpanded: implied check: uMaxIssuerID=%s uSenderIssuerID=%s uNxtCurrencyID=%s uNxtAccountID=%s")
                                       % RippleAddress::createHumanAccountID (uMaxIssuerID)
                                       % RippleAddress::createHumanAccountID (uSenderIssuerID)
                                       % STAmount::createHumanCurrency (uNxtCurrencyID)
                                       % RippleAddress::createHumanAccountID (uNxtAccountID));

        // Can't just use push implied, because it can't compensate for next account.
        if (!uNxtCurrencyID                         // Next is XRP, offer next. Must go through issuer.
                || uMaxCurrencyID != uNxtCurrencyID // Next is different currency, offer next...
                || uMaxIssuerID != uNxtAccountID)   // Next is not implied issuer
        {
            WriteLog (lsDEBUG, RippleCalc) << boost::str (boost::format ("setExpanded: sender implied: account=%s currency=%s issuer=%s")
                                           % RippleAddress::createHumanAccountID (uMaxIssuerID)
                                           % STAmount::createHumanCurrency (uMaxCurrencyID)
                                           % RippleAddress::createHumanAccountID (uMaxIssuerID));
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
            WriteLog (lsDEBUG, RippleCalc) << boost::str (boost::format ("setExpanded: element in path:"));
            terStatus   = pushNode (speElement.getNodeType (), speElement.getAccountID (), speElement.getCurrency (), speElement.getIssuerID ());
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
        WriteLog (lsDEBUG, RippleCalc) << boost::str (boost::format ("setExpanded: receiver implied: account=%s currency=%s issuer=%s")
                                       % RippleAddress::createHumanAccountID (uOutIssuerID)
                                       % STAmount::createHumanCurrency (uOutCurrencyID)
                                       % RippleAddress::createHumanAccountID (uOutIssuerID));
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

            if (!umForward.insert (std::make_pair (boost::make_tuple (pnCur.uAccountID, pnCur.uCurrencyID, pnCur.uIssuerID), uNode)).second)
            {
                // Failed to insert. Have a loop.
                WriteLog (lsDEBUG, RippleCalc) << boost::str (boost::format ("setExpanded: loop detected: %s")
                                               % getJson ());

                terStatus   = temBAD_PATH_LOOP;
            }
        }
    }

    WriteLog (lsDEBUG, RippleCalc) << boost::str (boost::format ("setExpanded: in=%s/%s out=%s/%s %s")
                                   % STAmount::createHumanCurrency (uMaxCurrencyID)
                                   % RippleAddress::createHumanAccountID (uMaxIssuerID)
                                   % STAmount::createHumanCurrency (uOutCurrencyID)
                                   % RippleAddress::createHumanAccountID (uOutIssuerID)
                                   % getJson ());
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

        const bool      bCurAccount     = isSetBit (pnCur.uFlags, STPathElement::typeAccount);

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
            const bool      bPrvAccount     = isSetBit (pnPrv.uFlags, STPathElement::typeAccount);
            const bool      bNxtAccount     = isSetBit (pnNxt.uFlags, STPathElement::typeAccount);

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

    WriteLog (lsDEBUG, RippleCalc) << boost::str (boost::format ("setCanonical: in=%s/%s out=%s/%s %s")
                                   % STAmount::createHumanCurrency (uMaxCurrencyID)
                                   % RippleAddress::createHumanAccountID (uMaxIssuerID)
                                   % STAmount::createHumanCurrency (uOutCurrencyID)
                                   % RippleAddress::createHumanAccountID (uOutIssuerID)
                                   % getJson ());
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
        jvPathState["uQuality"] = boost::str (boost::format ("%d") % uQuality);

    return jvPathState;
}

