//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/sync/ObjectRequester.h>

#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/LedgerHeader.h>
#include <ripple/shamap/SHAMapInnerNode.h>

namespace ripple {
namespace sync {

ObjectRequester::ObjectRequester(CopyLedger& copier) : copier_(copier)
{
    request_ = copier_.unsend();
}

void
ObjectRequester::deserialize(ObjectDigest const& digest, Slice const& slice)
{
    SerialIter sit(slice.data(), slice.size());
    auto prefix = safe_cast<HashPrefix>(sit.get32());
    if (prefix == HashPrefix::ledgerMaster)
    {
        // This is a ledger header.
        // REVIEWER: Are there names for these constants anywhere?
        assert(slice.size() == 4 + 118);
        auto sequence = sit.get32();
        JLOG(copier_.journal_.info())
            << "header,seq=" << sequence << ",digest=" << digest;
        sit.skip(8 + 32);
        auto txDigest = sit.get256();
        auto stateDigest = sit.get256();
        request(txDigest);
        request(stateDigest);
    }
    else if (prefix == HashPrefix::innerNode)
    {
        assert(slice.size() == 4 + SHAMapInnerNode::branchFactor * 32);
        // There's an opportunity here to exit early if the tree rooted at
        // this node is "full" (i.e. a complete copy is in the database).
        // There is a "full below cache" that might have that information,
        // but I cannot be certain that it is safe to use outside the context
        // of online delete.
        for (auto i = 0; i < SHAMapInnerNode::branchFactor; ++i)
        {
            auto childDigest = sit.get256();
            if (childDigest == beast::zero)
            {
                continue;
            }
            request(childDigest);
        }
    }
}

void
ObjectRequester::_request(ObjectDigest const& digest, std::size_t requested)
{
    ++searched_;

    // TODO: Load in batches.
    auto object = copier_.objectDatabase_.fetchNodeObject(digest);
    if (object)
    {
        ++loaded_;
        if (loaded_ % 100000 == 0)
        {
            JLOG(copier_.journal_.trace()) << "loaded: " << loaded_;
            // TODO: Load from disk in parallel, after loading a few hundred
            // with the same requester.
        }
        received_ += requested;
        deserialize(digest, object->getData());
        return;
    }

    if (!request_)
    {
        request_ = std::make_unique<RequestPtr::element_type>();
        request_->set_type(protocol::TMGetObjectByHash::otUNKNOWN);
    }

    request_->add_objects()->set_hash(digest.begin(), digest.size());
    requested_ += 1 - requested;

    if (request_->objects_size() >= CopyLedger::MAX_OBJECTS_PER_MESSAGE)
    {
        _send();
    }
}

void
ObjectRequester::_send()
{
    copier_.send(std::move(request_));
    assert(!request_);
}

ObjectRequester::~ObjectRequester()
{
    JLOG(copier_.journal_.trace())
        << "searched = loaded + requested + rerequested: " << searched_ << " = "
        << loaded_ << " + " << requested_ << " + "
        << (searched_ - loaded_ - requested_);
    if (request_)
    {
        _send();
    }
    if (requested_ || received_)
    {
        std::lock_guard lock(copier_.metricsMutex_);
        copier_.requested_ += requested_;
        copier_.received_ += received_;
    }
}

}  // namespace sync
}  // namespace ripple
