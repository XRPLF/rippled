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

SETUP_LOG (PathRequest)

// VFALCO TODO Move these globals into a PathRequests collection inteface
PathRequest::StaticLockType PathRequest::sLock ("PathRequest", __FILE__, __LINE__);
std::vector <PathRequest::wptr> PathRequest::sRequests;
RippleLineCache::pointer PathRequest::sLineCache;
Atomic<int> PathRequest::siLastIdentifier(0);

PathRequest::PathRequest (const boost::shared_ptr<InfoSub>& subscriber)
    : mLock (this, "PathRequest", __FILE__, __LINE__)
    , wpSubscriber (subscriber)
    , jvStatus (Json::objectValue)
    , bValid (false)
    , iLastIndex (0)
    , iLastLevel (0)
    , bLastSuccess (false)
    , iIdentifier (++siLastIdentifier)
{
    WriteLog (lsDEBUG, PathRequest) << iIdentifier << " created";
    ptCreated = boost::posix_time::microsec_clock::universal_time ();
}

static std::string const get_milli_diff (boost::posix_time::ptime const& after, boost::posix_time::ptime const& before)
{
    return lexicalCastThrow <std::string> (static_cast <unsigned> ((after - before).total_milliseconds()));
}

static std::string const get_milli_diff (boost::posix_time::ptime const& before)
{
    return get_milli_diff(boost::posix_time::microsec_clock::universal_time(), before);
}

PathRequest::~PathRequest()
{
    std::string fast, full;
    if (!ptQuickReply.is_not_a_date_time())
    {
        fast = " fast:";
        fast += get_milli_diff (ptQuickReply, ptCreated);
        fast += "ms";
    }
    if (!ptFullReply.is_not_a_date_time())
    {
        full = " full:";
        full += get_milli_diff (ptFullReply, ptCreated);
        full += "ms";
    }
    WriteLog (lsINFO, PathRequest) << iIdentifier << " complete:" << fast << full <<
        " total:" << get_milli_diff(ptCreated) << "ms";
}

bool PathRequest::isValid ()
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    return bValid;
}

bool PathRequest::needsUpdate (bool newOnly, LedgerIndex index)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    if (newOnly)
    { // we only want to handle new requests
        if (iLastIndex != 0)
            return false;
        iLastIndex = 1;
        return true;
    }
    else
    {
        if (iLastIndex >= index)
            return false;
        iLastIndex = index;
        return true;
    }
}

bool PathRequest::isValid (Ledger::ref lrLedger)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    bValid = raSrcAccount.isSet () && raDstAccount.isSet () && saDstAmount.isPositive ();

    if (bValid)
    {
        AccountState::pointer asSrc = getApp().getOPs ().getAccountState (lrLedger, raSrcAccount);

        if (!asSrc)
        {
            // no source account
            bValid = false;
            jvStatus = rpcError (rpcSRC_ACT_NOT_FOUND);
        }
        else
        {
            AccountState::pointer asDst = getApp().getOPs ().getAccountState (lrLedger, raDstAccount);
            Json::Value jvDestCur;

            if (!asDst)
            {
                // no destination account
                jvDestCur.append (Json::Value ("XRP"));

                if (!saDstAmount.isNative ())
                {
                    // only XRP can be send to a non-existent account
                    bValid = false;
                    jvStatus = rpcError (rpcACT_NOT_FOUND);
                }
                else if (saDstAmount < STAmount (lrLedger->getReserve (0)))
                {
                    // payment must meet reserve
                    bValid = false;
                    jvStatus = rpcError (rpcDST_AMT_MALFORMED);
                }
            }
            else
            {
                boost::unordered_set<uint160> usDestCurrID = usAccountDestCurrencies (raDstAccount, lrLedger, true);
                BOOST_FOREACH (const uint160 & uCurrency, usDestCurrID)
                jvDestCur.append (STAmount::createHumanCurrency (uCurrency));
                jvStatus["destination_tag"] = (asDst->peekSLE ().getFlags () & lsfRequireDestTag) != 0;
            }

            jvStatus["destination_currencies"] = jvDestCur;
        }
    }

    jvStatus["ledger_hash"] = lrLedger->getHash ().GetHex ();
    jvStatus["ledger_index"] = lrLedger->getLedgerSeq ();
    return bValid;
}

Json::Value PathRequest::doCreate (Ledger::ref lrLedger, const Json::Value& value)
{
    assert (lrLedger->isClosed ());

    // Get the ledger and line cache we should use
    Ledger::pointer ledger = lrLedger;
    RippleLineCache::pointer cache;
    {
        StaticScopedLockType sl (sLock, __FILE__, __LINE__);
        cache = getLineCache (ledger, false);
    }

    Json::Value status;
    bool mValid;

    {
        if (parseJson (value, true) != PFR_PJ_INVALID)
        {
            mValid = isValid (ledger);

            if (mValid)
                status = doUpdate (cache, true);
            else
                status = jvStatus;
        }
        else
        {
            mValid = false;
            status = jvStatus;
        }
    }

    if (mValid)
    {
        WriteLog (lsDEBUG, PathRequest) << iIdentifier << " valid: " << raSrcAccount.humanAccountID () <<
                                       " -> " << raDstAccount.humanAccountID ();
        WriteLog (lsDEBUG, PathRequest) << iIdentifier << " Deliver: " << saDstAmount.getFullText ();

        StaticScopedLockType sl (sLock, __FILE__, __LINE__);
        sRequests.push_back (shared_from_this ());
    }
    else
        WriteLog (lsDEBUG, PathRequest) << iIdentifier << " invalid";

    return status;
}

int PathRequest::parseJson (const Json::Value& jvParams, bool complete)
{
    int ret = PFR_PJ_NOCHANGE;

    if (jvParams.isMember ("source_account"))
    {
        if (!raSrcAccount.setAccountID (jvParams["source_account"].asString ()))
        {
            jvStatus = rpcError (rpcSRC_ACT_MALFORMED);
            return PFR_PJ_INVALID;
        }
    }
    else if (complete)
    {
        jvStatus = rpcError (rpcSRC_ACT_MISSING);
        return PFR_PJ_INVALID;
    }

    if (jvParams.isMember ("destination_account"))
    {
        if (!raDstAccount.setAccountID (jvParams["destination_account"].asString ()))
        {
            jvStatus = rpcError (rpcDST_ACT_MALFORMED);
            return PFR_PJ_INVALID;
        }
    }
    else if (complete)
    {
        jvStatus = rpcError (rpcDST_ACT_MISSING);
        return PFR_PJ_INVALID;
    }

    if (jvParams.isMember ("destination_amount"))
    {
        if (!saDstAmount.bSetJson (jvParams["destination_amount"]) ||
                (saDstAmount.getCurrency ().isZero () && saDstAmount.getIssuer ().isNonZero ()) ||
                (saDstAmount.getCurrency () == CURRENCY_BAD) ||
                !saDstAmount.isPositive ())
        {
            jvStatus = rpcError (rpcDST_AMT_MALFORMED);
            return PFR_PJ_INVALID;
        }
    }
    else if (complete)
    {
        jvStatus = rpcError (rpcDST_ACT_MISSING);
        return PFR_PJ_INVALID;
    }

    if (jvParams.isMember ("source_currencies"))
    {
        const Json::Value& jvSrcCur = jvParams["source_currencies"];

        if (!jvSrcCur.isArray ())
        {
            jvStatus = rpcError (rpcSRC_CUR_MALFORMED);
            return PFR_PJ_INVALID;
        }

        sciSourceCurrencies.clear ();

        for (unsigned i = 0; i < jvSrcCur.size (); ++i)
        {
            const Json::Value& jvCur = jvSrcCur[i];
            uint160 uCur, uIss;

            if (!jvCur.isObject() || !jvCur.isMember ("currency") || !STAmount::currencyFromString (uCur, jvCur["currency"].asString ()))
            {
                jvStatus = rpcError (rpcSRC_CUR_MALFORMED);
                return PFR_PJ_INVALID;
            }

            if (jvCur.isMember ("issuer") && !STAmount::issuerFromString (uIss, jvCur["issuer"].asString ()))
            {
                jvStatus = rpcError (rpcSRC_ISR_MALFORMED);
            }

            if (uCur.isZero () && uIss.isNonZero ())
            {
                jvStatus = rpcError (rpcSRC_CUR_MALFORMED);
                return PFR_PJ_INVALID;
            }

            sciSourceCurrencies.insert (currIssuer_t (uCur, uIss));
        }
    }

    if (jvParams.isMember ("id"))
        jvId = jvParams["id"];

    return ret;
}
Json::Value PathRequest::doClose (const Json::Value&)
{
    WriteLog (lsDEBUG, PathRequest) << iIdentifier << " closed";
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    return jvStatus;
}

Json::Value PathRequest::doStatus (const Json::Value&)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);
    return jvStatus;
}

void PathRequest::resetLevel (int l)
{
    if (iLastLevel > l)
        iLastLevel = l;
}

Json::Value PathRequest::doUpdate (RippleLineCache::ref cache, bool fast)
{
    WriteLog (lsDEBUG, PathRequest) << iIdentifier << " update " << (fast ? "fast" : "normal");
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    if (!isValid (cache->getLedger ()))
        return jvStatus;
    jvStatus = Json::objectValue;

    std::set<currIssuer_t> sourceCurrencies (sciSourceCurrencies);

    if (sourceCurrencies.empty ())
    {
        boost::unordered_set<uint160> usCurrencies =
            usAccountSourceCurrencies (raSrcAccount, cache->getLedger (), true);
        bool sameAccount = raSrcAccount == raDstAccount;
        BOOST_FOREACH (const uint160 & c, usCurrencies)
        {
            if (!sameAccount || (c != saDstAmount.getCurrency ()))
            {
                if (c.isZero ())
                    sourceCurrencies.insert (std::make_pair (c, ACCOUNT_XRP));
                else
                    sourceCurrencies.insert (std::make_pair (c, raSrcAccount.getAccountID ()));
            }
        }
    }

    jvStatus["source_account"] = raSrcAccount.humanAccountID ();
    jvStatus["destination_account"] = raDstAccount.humanAccountID ();
    jvStatus["destination_amount"] = saDstAmount.getJson (0);

    if (!jvId.isNull ())
        jvStatus["id"] = jvId;

    Json::Value jvArray = Json::arrayValue;

    int iLevel = iLastLevel;
    bool loaded = getApp().getFeeTrack().isLoadedLocal();

    if (iLevel == 0)
    { // first pass
        if (loaded || fast)
            iLevel = getConfig().PATH_SEARCH_FAST;
        else
            iLevel = getConfig().PATH_SEARCH;
    }
    else if ((iLevel == getConfig().PATH_SEARCH_FAST) && !fast)
    { // leaving fast pathfinding
        iLevel = getConfig().PATH_SEARCH;
        if (loaded && (iLevel > getConfig().PATH_SEARCH_FAST))
            --iLevel;
    }
    else if (bLastSuccess)
    { // decrement, if possible
        if ((iLevel > getConfig().PATH_SEARCH) || (loaded && (iLevel > getConfig().PATH_SEARCH_FAST)))
            --iLevel;
    }
    else
    { // adjust as needed
        if (!loaded && (iLevel < getConfig().PATH_SEARCH_MAX))
            ++iLevel;
        if (loaded && (iLevel > getConfig().PATH_SEARCH_FAST))
            --iLevel;
    }

    WriteLog (lsDEBUG, PathRequest) << iIdentifier << " processing at level " << iLevel;

    bool found = false;

    BOOST_FOREACH (const currIssuer_t & currIssuer, sourceCurrencies)
    {
        {
            STAmount test (currIssuer.first, currIssuer.second, 1);
            WriteLog (lsDEBUG, PathRequest) << iIdentifier << " Trying to find paths: " << test.getFullText ();
        }
        bool valid;
        STPathSet& spsPaths = mContext[currIssuer];
        Pathfinder pf (cache, raSrcAccount, raDstAccount,
                       currIssuer.first, currIssuer.second, saDstAmount, valid);
        CondLog (!valid, lsDEBUG, PathRequest) << iIdentifier << " PF request not valid";

        STPath extraPath;
        if (valid && pf.findPaths (iLevel, 4, spsPaths, extraPath))
        {
            LedgerEntrySet                      lesSandbox (cache->getLedger (), tapNONE);
            std::vector<PathState::pointer>     vpsExpanded;
            STAmount                            saMaxAmountAct;
            STAmount                            saDstAmountAct;
            STAmount                            saMaxAmount (currIssuer.first,
                    currIssuer.second.isNonZero () ? currIssuer.second :
                    (currIssuer.first.isZero () ? ACCOUNT_XRP : raSrcAccount.getAccountID ()), 1);
            saMaxAmount.negate ();
            WriteLog (lsDEBUG, PathRequest) << iIdentifier << " Paths found, calling rippleCalc";
            TER terResult = RippleCalc::rippleCalc (lesSandbox, saMaxAmountAct, saDstAmountAct,
                                                    vpsExpanded, saMaxAmount, saDstAmount,
                                                    raDstAccount.getAccountID (), raSrcAccount.getAccountID (),
                                                    spsPaths, false, false, false, true);


            if ((extraPath.size() > 0) && ((terResult == terNO_LINE) || (terResult == tecPATH_PARTIAL)))
            {
                WriteLog (lsDEBUG, PathRequest) << iIdentifier << " Trying with an extra path element";
                spsPaths.addPath(extraPath);
                vpsExpanded.clear ();
                terResult = RippleCalc::rippleCalc (lesSandbox, saMaxAmountAct, saDstAmountAct,
                                                    vpsExpanded, saMaxAmount, saDstAmount,
                                                    raDstAccount.getAccountID (), raSrcAccount.getAccountID (),
                                                    spsPaths, false, false, false, true);
                WriteLog (lsDEBUG, PathRequest) << iIdentifier << " Extra path element gives " << transHuman (terResult);
            }

            if (terResult == tesSUCCESS)
            {
                Json::Value jvEntry (Json::objectValue);
                jvEntry["source_amount"]    = saMaxAmountAct.getJson (0);
                jvEntry["paths_computed"]   = spsPaths.getJson (0);
                found  = true;
                jvArray.append (jvEntry);
            }
            else
            {
                WriteLog (lsDEBUG, PathRequest) << iIdentifier << " rippleCalc returns " << transHuman (terResult);
            }
        }
        else
        {
            WriteLog (lsDEBUG, PathRequest) << iIdentifier << " No paths found";
        }
    }

    iLastLevel = iLevel;
    bLastSuccess = found;

    if (fast && ptQuickReply.is_not_a_date_time())
        ptQuickReply = boost::posix_time::microsec_clock::universal_time();
    else if (!fast && ptFullReply.is_not_a_date_time())
        ptFullReply = boost::posix_time::microsec_clock::universal_time();

    jvStatus["alternatives"] = jvArray;
    return jvStatus;
}

/** Get the current RippleLineCache, updating it if necessary.
    Get the correct ledger to use.
    Call with a lock
*/
RippleLineCache::pointer PathRequest::getLineCache (Ledger::pointer& ledger, bool authoritative)
{
    uint32 lineSeq = sLineCache ? sLineCache->getLedger()->getLedgerSeq() : 0;
    uint32 lgrSeq = ledger->getLedgerSeq();

    if ( (lineSeq == 0) ||                                 // no ledger
         (authoritative && (lgrSeq > lineSeq)) ||          // newer authoritative ledger
         (authoritative && ((lgrSeq + 8)  < lineSeq)) ||   // we jumped way back for some reason
         (lgrSeq > (lineSeq + 8)))                         // we jumped way forward for some reason
    {
        ledger = boost::make_shared<Ledger>(*ledger, false); // Take a snapshot of the ledger
        sLineCache = boost::make_shared<RippleLineCache> (ledger);
    }
    else
    {
        ledger = sLineCache->getLedger();
    }
    return sLineCache;
}

void PathRequest::updateAll (Ledger::ref inLedger, CancelCallback shouldCancel)
{
    std::vector<wptr> requests;

    LoadEvent::autoptr event (getApp().getJobQueue().getLoadEventAP(jtPATH_FIND, "PathRequest::updateAll"));

    // Get the ledger and cache we should be using
    Ledger::pointer ledger = inLedger;
    RippleLineCache::pointer cache;
    {
        StaticScopedLockType sl (sLock, __FILE__, __LINE__);
        requests = sRequests;
        cache = getLineCache (ledger, true);
    }

    bool newRequests = getApp().getLedgerMaster().isNewPathRequest();
    bool mustBreak = false;

    WriteLog (lsTRACE, PathRequest) << "updateAll seq=" << ledger->getLedgerSeq() << ", " <<
        requests.size() << " requests";
    int processed = 0, removed = 0;

    do
    {

        { // Get the latest requests, cache, and ledger
            StaticScopedLockType sl (sLock, __FILE__, __LINE__);

            if (sRequests.empty())
                return;

            // Newest request is last in sRequests, but we want to serve it first
            requests.empty();
            requests.reserve(sRequests.size ());
            BOOST_REVERSE_FOREACH (wptr& req, sRequests)
            {
               requests.push_back (req);
            }

            cache = getLineCache (ledger, false);
        }

        BOOST_FOREACH (wref wRequest, requests)
        {
            if (shouldCancel())
                break;

            bool remove = true;
            PathRequest::pointer pRequest = wRequest.lock ();

            if (pRequest)
            {
                if (!pRequest->needsUpdate (newRequests, ledger->getLedgerSeq ()))
                    remove = false;
                else
                {
                    InfoSub::pointer ipSub = pRequest->wpSubscriber.lock ();

                    if (ipSub)
                    {
                        Json::Value update = pRequest->doUpdate (cache, false);
                        update["type"] = "path_find";
                        ipSub->send (update, false);
                        remove = false;
                        ++processed;
                    }
                }
            }

            if (remove)
            {
                PathRequest::pointer pRequest = wRequest.lock ();

                StaticScopedLockType sl (sLock, __FILE__, __LINE__);

                // Remove any dangling weak pointers or weak pointers that refer to this path request.
                std::vector<wptr>::iterator it = sRequests.begin();
                while (it != sRequests.end())
                {
                    PathRequest::pointer itRequest = it->lock ();
                    if (!itRequest || (itRequest == pRequest))
                    {
                        ++removed;
                        it = sRequests.erase (it);
                    }
                    else
                        ++it;
                }
            }

            mustBreak = !newRequests && getApp().getLedgerMaster().isNewPathRequest();
            if (mustBreak) // We weren't handling new requests and then there was a new request
                break;

        }

        if (mustBreak)
        { // a new request came in while we were working
            newRequests = true;
        }
        else if (newRequests)
        { // we only did new requests, so we always need a last pass
            newRequests = getApp().getLedgerMaster().isNewPathRequest();
        }
        else
        { // check if there are any new requests, otherwise we are done
            newRequests = getApp().getLedgerMaster().isNewPathRequest();
            if (!newRequests) // We did a full pass and there are no new requests
                return;
        }

    }
    while (!shouldCancel ());

    WriteLog (lsDEBUG, PathRequest) << "updateAll complete " << processed << " process and " <<
        removed << " removed";
}

// vim:ts=4
