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

/** Get the current RippleLineCache, updating it if necessary.
    Get the correct ledger to use.
*/
RippleLineCache::pointer PathRequests::getLineCache (Ledger::pointer& ledger, bool authoritative)
{
    ScopedLockType sl (mLock);

    std::uint32_t lineSeq = mLineCache ? mLineCache->getLedger()->getLedgerSeq() : 0;
    std::uint32_t lgrSeq = ledger->getLedgerSeq();

    if ( (lineSeq == 0) ||                                 // no ledger
         (authoritative && (lgrSeq > lineSeq)) ||          // newer authoritative ledger
         (authoritative && ((lgrSeq + 8)  < lineSeq)) ||   // we jumped way back for some reason
         (lgrSeq > (lineSeq + 8)))                         // we jumped way forward for some reason
    {
        ledger = std::make_shared<Ledger>(*ledger, false); // Take a snapshot of the ledger
        mLineCache = std::make_shared<RippleLineCache> (ledger);
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
        ScopedLockType sl (mLock);
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
                        ipSub->getConsumer ().charge (Resource::feePathFindUpdate);
                        if (!ipSub->getConsumer ().warn ())
                        {
                            Json::Value update = pRequest->doUpdate (cache, false);
                            pRequest->updateComplete ();
                            update["type"] = "path_find";
                            ipSub->send (update, false);
                            remove = false;
                            ++processed;
                        }
                    }
                }
            }

            if (remove)
            {
                PathRequest::pointer pRequest = wRequest.lock ();

                ScopedLockType sl (mLock);

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

        {
            // Get the latest requests, cache, and ledger for next pass
            ScopedLockType sl (mLock);

            if (mRequests.empty())
                break;
            requests = mRequests;

            cache = getLineCache (ledger, false);
        }

    }
    while (!shouldCancel ());

    mJournal.debug << "updateAll complete " << processed << " process and " <<
        removed << " removed";
}

Json::Value PathRequests::makePathRequest(
    std::shared_ptr <InfoSub> const& subscriber,
    const std::shared_ptr<Ledger>& inLedger,
    const Json::Value& requestJson)
{
    PathRequest::pointer req = std::make_shared<PathRequest> (
        subscriber, ++mLastIdentifier, *this, mJournal);

    Ledger::pointer ledger = inLedger;
    RippleLineCache::pointer cache;

    {
        ScopedLockType sl (mLock);
        cache = getLineCache (ledger, false);
    }

    bool valid = false;
    Json::Value result = req->doCreate (ledger, cache, requestJson, valid);

    if (valid)
    {
        {
            ScopedLockType sl (mLock);

            // Insert after any older unserviced requests but before any serviced requests
            std::vector<PathRequest::wptr>::iterator it = mRequests.begin ();
            while (it != mRequests.end ())
            {
                PathRequest::pointer req = it->lock ();
                if (req && !req->isNew ())
                    break; // This request has been handled, we come before it

                // This is a newer request, we come after it
                ++it;
            }
            mRequests.insert (it, PathRequest::wptr (req));

        }
        subscriber->setPathRequest (req);
        getApp().getLedgerMaster().newPathRequest();
    }
    return result;
}

} // ripple
