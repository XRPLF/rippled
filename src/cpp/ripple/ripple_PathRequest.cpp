SETUP_LOG (PathRequest)

// VFALCO TODO Move these globals into a PathRequests collection inteface
boost::recursive_mutex       PathRequest::sLock;
std::set <PathRequest::wptr> PathRequest::sRequests;

PathRequest::PathRequest (const boost::shared_ptr<InfoSub>& subscriber)
    : wpSubscriber (subscriber)
    , jvStatus (Json::objectValue)
    , bValid (false)
    , bNew (true)
{
}

bool PathRequest::isValid ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    return bValid;
}

bool PathRequest::isNew ()
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    return bNew;
}

bool PathRequest::isValid (Ledger::ref lrLedger)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    bValid = raSrcAccount.isSet () && raDstAccount.isSet () && saDstAmount.isPositive ();

    if (bValid)
    {
        AccountState::pointer asSrc = theApp->getOPs ().getAccountState (lrLedger, raSrcAccount);

        if (!asSrc)
        {
            // no source account
            bValid = false;
            jvStatus = rpcError (rpcSRC_ACT_NOT_FOUND);
        }
        else
        {
            AccountState::pointer asDst = theApp->getOPs ().getAccountState (lrLedger, raDstAccount);
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

    Json::Value status;
    bool mValid;

    {
        boost::recursive_mutex::scoped_lock sl (mLock);

        if (parseJson (value, true) != PFR_PJ_INVALID)
        {
            mValid = isValid (lrLedger);

            if (mValid)
            {
                RLCache::pointer cache = boost::make_shared<RLCache> (lrLedger);
                doUpdate (cache, true);
            }
        }
        else
            mValid = false;
    }

    if (mValid)
    {
        WriteLog (lsINFO, PathRequest) << "Request created: " << raSrcAccount.humanAccountID () <<
                                       " -> " << raDstAccount.humanAccountID ();
        WriteLog (lsINFO, PathRequest) << "Deliver: " << saDstAmount.getFullText ();

        boost::recursive_mutex::scoped_lock sl (sLock);
        sRequests.insert (shared_from_this ());
    }

    return jvStatus;
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

            if (!jvCur.isMember ("currency") || !STAmount::currencyFromString (uCur, jvCur["currency"].asString ()))
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
    boost::recursive_mutex::scoped_lock sl (mLock);
    return jvStatus;
}

Json::Value PathRequest::doStatus (const Json::Value&)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    return jvStatus;
}

bool PathRequest::doUpdate (RLCache::ref cache, bool fast)
{
    boost::recursive_mutex::scoped_lock sl (mLock);
    jvStatus = Json::objectValue;

    if (!isValid (cache->getLedger ()))
        return false;

    if (!fast)
        bNew = false;

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

    BOOST_FOREACH (const currIssuer_t & currIssuer, sourceCurrencies)
    {
        {
            STAmount test (currIssuer.first, currIssuer.second, 1);
            WriteLog (lsDEBUG, PathRequest) << "Trying to find paths: " << test.getFullText ();
        }
        bool valid;
        STPathSet spsPaths;
        Pathfinder pf (cache, raSrcAccount, raDstAccount,
                       currIssuer.first, currIssuer.second, saDstAmount, valid);
        CondLog (!valid, lsINFO, PathRequest) << "PF request not valid";

        if (valid && pf.findPaths (theConfig.PATH_SEARCH_SIZE - (fast ? 0 : 1), 3, spsPaths))
        {
            LedgerEntrySet                      lesSandbox (cache->getLedger (), tapNONE);
            std::vector<PathState::pointer>     vpsExpanded;
            STAmount                            saMaxAmountAct;
            STAmount                            saDstAmountAct;
            STAmount                            saMaxAmount (currIssuer.first,
                    currIssuer.second.isNonZero () ? currIssuer.second :
                    (currIssuer.first.isZero () ? ACCOUNT_XRP : raSrcAccount.getAccountID ()), 1);
            saMaxAmount.negate ();
            WriteLog (lsDEBUG, PathRequest) << "Paths found, calling rippleCalc";
            TER terResult = RippleCalc::rippleCalc (lesSandbox, saMaxAmountAct, saDstAmountAct,
                                                    vpsExpanded, saMaxAmount, saDstAmount, raDstAccount.getAccountID (), raSrcAccount.getAccountID (),
                                                    spsPaths, false, false, false, true);

            if (terResult == tesSUCCESS)
            {
                Json::Value jvEntry (Json::objectValue);
                jvEntry["source_amount"]    = saMaxAmountAct.getJson (0);
                jvEntry["paths_computed"]   = spsPaths.getJson (0);
                jvArray.append (jvEntry);
            }
            else
            {
                WriteLog (lsINFO, PathRequest) << "rippleCalc returns " << transHuman (terResult);
            }
        }
        else
        {
            WriteLog (lsINFO, PathRequest) << "No paths found";
        }
    }
    jvStatus["alternatives"] = jvArray;
    return true;
}

void PathRequest::updateAll (Ledger::ref ledger, bool newOnly)
{
    std::set<wptr> requests;

    {
        boost::recursive_mutex::scoped_lock sl (sLock);
        requests = sRequests;
    }

    if (requests.empty ())
        return;

    RLCache::pointer cache = boost::make_shared<RLCache> (ledger);

    BOOST_FOREACH (wref wRequest, requests)
    {
        bool remove = true;
        PathRequest::pointer pRequest = wRequest.lock ();

        if (pRequest && (!newOnly || pRequest->isNew ()))
        {
            InfoSub::pointer ipSub = pRequest->wpSubscriber.lock ();

            if (ipSub)
            {
                Json::Value update;
                {
                    boost::recursive_mutex::scoped_lock sl (pRequest->mLock);
                    pRequest->doUpdate (cache, false);
                    update = pRequest->jvStatus;
                }
                update["type"] = "path_find";
                ipSub->send (update, false);
                remove = false;
            }
        }

        if (remove)
        {
            boost::recursive_mutex::scoped_lock sl (sLock);
            sRequests.erase (wRequest);
        }
    }
}

// vim:ts=4
