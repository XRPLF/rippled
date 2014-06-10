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

#include <tuple>
#include <boost/log/trivial.hpp>

#include <ripple/module/app/paths/Calculators.h>

namespace ripple {

SETUP_LOG (PathRequest)

PathRequest::PathRequest (
const std::shared_ptr<InfoSub>& subscriber, int id, PathRequests& owner,
    beast::Journal journal)
    : m_journal (journal)
    , mOwner (owner)
    , wpSubscriber (subscriber)
    , jvStatus (Json::objectValue)
    , bValid (false)
    , mLastIndex (0)
    , mInProgress (false)
    , iLastLevel (0)
    , bLastSuccess (false)
    , iIdentifier (id)
{
    if (m_journal.debug)
        m_journal.debug << iIdentifier << " created";
    ptCreated = boost::posix_time::microsec_clock::universal_time ();
}

static std::string const get_milli_diff (boost::posix_time::ptime const& after, boost::posix_time::ptime const& before)
{
    return beast::lexicalCastThrow <std::string> (static_cast <unsigned> ((after - before).total_milliseconds()));
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
    if (m_journal.info)
        m_journal.info << iIdentifier << " complete:" << fast << full <<
        " total:" << get_milli_diff(ptCreated) << "ms";
}

bool PathRequest::isValid ()
{
    ScopedLockType sl (mLock);
    return bValid;
}

bool PathRequest::isNew ()
{
    ScopedLockType sl (mIndexLock);

    // does this path request still need its first full path
    return mLastIndex == 0;
}

bool PathRequest::needsUpdate (bool newOnly, LedgerIndex index)
{
    ScopedLockType sl (mIndexLock);

    if (mInProgress)
    {
        // Another thread is handling this
        return false;
    }

    if (newOnly && (mLastIndex != 0))
    {
        // Only handling new requests, this isn't new
        return false;
    }

    if (mLastIndex >= index)
    {
        return false;
    }

    mInProgress = true;
    return true;
}

void PathRequest::updateComplete ()
{
    ScopedLockType sl (mIndexLock);

    assert (mInProgress);
    mInProgress = false;
}

bool PathRequest::isValid (RippleLineCache::ref crCache)
{
    ScopedLockType sl (mLock);
    bValid = raSrcAccount.isSet () && raDstAccount.isSet () && saDstAmount > zero;
    Ledger::pointer lrLedger = crCache->getLedger ();

    if (bValid)
    {
        AccountState::pointer asSrc = getApp().getOPs ().getAccountState (crCache->getLedger(), raSrcAccount);

        if (!asSrc)
        {
            // no source account
            bValid = false;
            jvStatus = rpcError (rpcSRC_ACT_NOT_FOUND);
        }
        else
        {
            AccountState::pointer asDst = getApp().getOPs ().getAccountState (lrLedger, raDstAccount);

            Json::Value& jvDestCur = (jvStatus["destination_currencies"] = Json::arrayValue);

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
                bool const disallowXRP (
                    asDst->peekSLE ().getFlags() & lsfDisallowXRP);

                boost::unordered_set<uint160> usDestCurrID =
                    usAccountDestCurrencies (raDstAccount, crCache, !disallowXRP);

                for (auto const& currency : usDestCurrID)
                    jvDestCur.append (STAmount::createHumanCurrency (currency));

                jvStatus["destination_tag"] = (asDst->peekSLE ().getFlags () & lsfRequireDestTag) != 0;
            }
        }
    }

    if (bValid)
    {
        jvStatus["ledger_hash"] = to_string (lrLedger->getHash ());
        jvStatus["ledger_index"] = lrLedger->getLedgerSeq ();
    }
    return bValid;
}

Json::Value PathRequest::doCreate (Ledger::ref lrLedger, RippleLineCache::ref& cache,
    const Json::Value& value, bool& valid)
{

    Json::Value status;

    if (parseJson (value, true) != PFR_PJ_INVALID)
    {
        bValid = isValid (cache);

        if (bValid)
            status = doUpdate (cache, true);
        else
            status = jvStatus;
    }
    else
    {
        bValid = false;
        status = jvStatus;
    }

    if (m_journal.debug)
    {
        if (bValid)
        {
            m_journal.debug << iIdentifier << " valid: " << raSrcAccount.humanAccountID ();
            m_journal.debug << iIdentifier << " Deliver: " << saDstAmount.getFullText ();
        }
        else
            m_journal.debug << iIdentifier << " invalid";
    }

    valid = bValid;
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
                saDstAmount <= zero)
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
    m_journal.debug << iIdentifier << " closed";
    ScopedLockType sl (mLock);
    return jvStatus;
}

Json::Value PathRequest::doStatus (const Json::Value&)
{
    ScopedLockType sl (mLock);
    return jvStatus;
}

void PathRequest::resetLevel (int l)
{
    if (iLastLevel > l)
        iLastLevel = l;
}

Json::Value PathRequest::doUpdate (RippleLineCache::ref cache, bool fast)
{
    m_journal.debug << iIdentifier << " update " << (fast ? "fast" : "normal");

    ScopedLockType sl (mLock);

    if (!isValid (cache))
        return jvStatus;
    jvStatus = Json::objectValue;

    std::set<currIssuer_t> sourceCurrencies (sciSourceCurrencies);

    if (sourceCurrencies.empty ())
    {
        boost::unordered_set<uint160> usCurrencies =
            usAccountSourceCurrencies (raSrcAccount, cache, true);
        bool sameAccount = raSrcAccount == raDstAccount;
        BOOST_FOREACH (const uint160 & c, usCurrencies)
        {
            if (!sameAccount || (c != saDstAmount.getCurrency ()))
            {
                if (c.isZero ())
                    sourceCurrencies.insert (std::make_pair (c, XRP_ACCOUNT));
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

    m_journal.debug << iIdentifier << " processing at level " << iLevel;

    bool found = false;

    BOOST_FOREACH (const currIssuer_t & currIssuer, sourceCurrencies)
    {
        {
            STAmount test (currIssuer.first, currIssuer.second, 1);
            if (m_journal.debug)
                m_journal.debug << iIdentifier << " Trying to find paths: " << test.getFullText ();
        }
        bool valid;
        STPathSet& spsPaths = mContext[currIssuer];
        Pathfinder pf (cache, raSrcAccount, raDstAccount,
                       currIssuer.first, currIssuer.second, saDstAmount, valid);
        CondLog (!valid, lsDEBUG, PathRequest) << iIdentifier << " PF request not valid";

        STPath extraPath;
        if (valid && pf.findPaths (iLevel, 4, spsPaths, extraPath))
        {
            LedgerEntrySet lesSandbox (cache->getLedger (), tapNONE);
            PathState::List pathStateList;
            STAmount saMaxAmountAct;
            STAmount saDstAmountAct;
            STAmount saMaxAmount (currIssuer.first,
                    currIssuer.second.isNonZero () ? currIssuer.second :
                                  (currIssuer.first.isZero () ? ACCOUNT_XRP :
                                   raSrcAccount.getAccountID ()), 1);
            saMaxAmount.negate ();
            m_journal.debug << iIdentifier << " Paths found, calling rippleCalc";
            TER resultCode = path::rippleCalculate (lesSandbox, saMaxAmountAct, saDstAmountAct,
                                                    pathStateList, saMaxAmount, saDstAmount,
                                                    raDstAccount.getAccountID (), raSrcAccount.getAccountID (),
                                                    spsPaths, false, false, false, true);


            if ((extraPath.size() > 0) && ((resultCode == terNO_LINE) || (resultCode == tecPATH_PARTIAL)))
            {
                m_journal.debug << iIdentifier << " Trying with an extra path element";
                spsPaths.addPath(extraPath);
                pathStateList.clear ();
                resultCode = path::rippleCalculate (lesSandbox, saMaxAmountAct, saDstAmountAct,
                                                    pathStateList, saMaxAmount, saDstAmount,
                                                    raDstAccount.getAccountID (), raSrcAccount.getAccountID (),
                                                    spsPaths, false, false, false, true);
                m_journal.debug << iIdentifier << " Extra path element gives " << transHuman (resultCode);
            }

            if (resultCode == tesSUCCESS)
            {
                Json::Value jvEntry (Json::objectValue);
                jvEntry["source_amount"]    = saMaxAmountAct.getJson (0);
                jvEntry["paths_computed"]   = spsPaths.getJson (0);
                found  = true;
                jvArray.append (jvEntry);
            }
            else
            {
                m_journal.debug << iIdentifier << " rippleCalc returns " << transHuman (resultCode);
            }
        }
        else
        {
            m_journal.debug << iIdentifier << " No paths found";
        }
    }

    iLastLevel = iLevel;
    bLastSuccess = found;

    if (fast && ptQuickReply.is_not_a_date_time())
    {
        ptQuickReply = boost::posix_time::microsec_clock::universal_time();
        mOwner.reportFast ((ptQuickReply-ptCreated).total_milliseconds());
    }
    else if (!fast && ptFullReply.is_not_a_date_time())
    {
        ptFullReply = boost::posix_time::microsec_clock::universal_time();
        mOwner.reportFull ((ptFullReply-ptCreated).total_milliseconds());
    }

    jvStatus["alternatives"] = jvArray;
    return jvStatus;
}

InfoSub::pointer PathRequest::getSubscriber ()
{
    return wpSubscriber.lock ();
}

} // ripple
