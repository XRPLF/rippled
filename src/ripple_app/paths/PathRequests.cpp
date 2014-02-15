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

/** Get the current RippleLineCache, updating it if necessary.
    Get the correct ledger to use.
*/
RippleLineCache::pointer PathRequests::getLineCache (Ledger::pointer& ledger, bool authoritative)
{
    ScopedLockType sl (mLock, __FILE__, __LINE__);

    uint32 lineSeq = mLineCache ? mLineCache->getLedger()->getLedgerSeq() : 0;
    uint32 lgrSeq = ledger->getLedgerSeq();

    if ( (lineSeq == 0) ||                                 // no ledger
         (authoritative && (lgrSeq > lineSeq)) ||          // newer authoritative ledger
         (authoritative && ((lgrSeq + 8)  < lineSeq)) ||   // we jumped way back for some reason
         (lgrSeq > (lineSeq + 8)))                         // we jumped way forward for some reason
    {
        ledger = boost::make_shared<Ledger>(*ledger, false); // Take a snapshot of the ledger
        mLineCache = boost::make_shared<RippleLineCache> (ledger);
    }
    else
    {
        ledger = mLineCache->getLedger();
    }
    return mLineCache;
}

void PathRequests::updateAll (Ledger::ref inLedger, CancelCallback shouldCancel)
{
    std::vector<PathRequest::wptr> requests;

    LoadEvent::autoptr event (getApp().getJobQueue().getLoadEventAP(jtPATH_FIND, "PathRequest::updateAll"));

    // Get the ledger and cache we should be using
    Ledger::pointer ledger = inLedger;
    RippleLineCache::pointer cache;
    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        requests = mRequests;
        cache = getLineCache (ledger, true);
    }

    bool newRequests = getApp().getLedgerMaster().isNewPathRequest();
    bool mustBreak = false;

    mJournal.trace << "updateAll seq=" << ledger->getLedgerSeq() << ", " <<
        requests.size() << " requests";
    int processed = 0, removed = 0;

    do
    {

        { // Get the latest requests, cache, and ledger
            ScopedLockType sl (mLock, __FILE__, __LINE__);

            if (mRequests.empty())
                return;

            // Newest request is last in mRequests, but we want to serve it first
            requests.clear();
            requests.reserve (mRequests.size ());
            BOOST_REVERSE_FOREACH (PathRequest::wptr& req, mRequests)
            {
               requests.push_back (req);
            }

            cache = getLineCache (ledger, false);
        }

        BOOST_FOREACH (PathRequest::wref wRequest, requests)
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
                    InfoSub::pointer ipSub = pRequest->getSubscriber ();
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

                ScopedLockType sl (mLock, __FILE__, __LINE__);

                // Remove any dangling weak pointers or weak pointers that refer to this path request.
                std::vector<PathRequest::wptr>::iterator it = mRequests.begin();
                while (it != mRequests.end())
                {
                    PathRequest::pointer itRequest = it->lock ();
                    if (!itRequest || (itRequest == pRequest))
                    {
                        ++removed;
                        it = mRequests.erase (it);
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

    mJournal.debug << "updateAll complete " << processed << " process and " <<
        removed << " removed";
}

Json::Value PathRequests::makePathRequest(
    boost::shared_ptr <InfoSub> const& subscriber,
    const boost::shared_ptr<Ledger>& inLedger,
    const Json::Value& requestJson)
{
    PathRequest::pointer req = boost::make_shared<PathRequest> (
        subscriber, ++mLastIdentifier, *this, mJournal);

    Ledger::pointer ledger = inLedger;
    RippleLineCache::pointer cache;

    {
        ScopedLockType sl (mLock, __FILE__, __LINE__);
        cache = getLineCache (ledger, false);
    }

    bool valid = false;
    Json::Value result = req->doCreate (ledger, cache, requestJson, valid);

    if (valid)
    {
        {
            ScopedLockType sl (mLock, __FILE__, __LINE__);
            mRequests.push_back (req);
        }
        subscriber->setPathRequest (req);
        getApp().getLedgerMaster().newPathRequest();
    }
    return result;
}
                        

// vim:ts=4
