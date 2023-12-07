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

#include <ripple/protocol/digest.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/LedgerHeader.h>
#include <ripple/shamap/SHAMapInnerNode.h>

namespace ripple {
namespace sync {

ObjectRequester::ObjectRequester(CopyLedger& copier, CopyLedger::Metrics& metrics) : copier_(copier), metrics_(metrics)
{
    request_ = copier_.unsend();
}

void
ObjectRequester::receive(RequestPtr const& request, protocol::TMGetObjectByHash const& response)
{
    // `i` is the index in the request. `j` is the index in the response.
    int i = 0;
    for (int j = 0; j < response.objects_size(); ++j)
    {
        auto const& object = response.objects(j);

        // For these first two tests,
        // we can't even be sure what object was requested,
        // so there is no fix.

        if (!object.has_hash())
        {
            JLOG(copier_.journal_.warn()) << copier_.digest_ << " object is missing digest";
            ++metrics_.errors;
            continue;
        }

        if (object.hash().size() != ObjectDigest::size())
        {
            JLOG(copier_.journal_.warn()) << copier_.digest_ << " digest is wrong size";
            ++metrics_.errors;
            continue;
        }

        // We assume the response holds a subset of the objects requested,
        // and that objects appear in the response in the same order as
        // their digests appear in the request.
        // Thus, if this object in the response does not match the next
        // object requested, then we conclude the requested object is
        // missing from the response, and repeat until we find a match.
        while (true)
        {
            if (i >= request->objects_size())
            {
                // The remaining objects in the response are unrequested.
                metrics_.extra += response.objects_size() - j;
                return;
            }
            auto const& ihash = request->objects(i).hash();
            ++i;
            if (ihash == object.hash())
            {
                break;
            }
            auto idigest = ObjectDigest(ihash);
            ++metrics_.missing;
            _rerequest(idigest);
        }

        // For the remaining tests,
        // if they fail,
        // then we should request the object again
        // (from a different peer).

        // This copies. Sad.
        auto digest = ObjectDigest(object.hash());

        if (!object.has_data())
        {
            JLOG(copier_.journal_.warn()) << "missing data: " << digest;
            ++metrics_.errors;
            _rerequest(digest);
            continue;
        }

        auto slice = makeSlice(object.data());

        // REVIEWER: Can we get rid of this expensive check?
        if (digest != sha512Half(slice))
        {
            JLOG(copier_.journal_.warn()) << "wrong digest";
            ++metrics_.errors;
            _rerequest(digest);
            continue;
        }

        ++metrics_.dreceived;

        _deserialize(digest, slice);

        NodeObjectType type = NodeObjectType::hotUNKNOWN;
        Blob blob(slice.begin(), slice.end());
        std::uint32_t ledgerSeq = 0;
        copier_.objectDatabase_.store(type, std::move(blob), digest, ledgerSeq);
    }

    metrics_.missing += request->objects_size() - i;
    for (; i < request->objects_size(); ++i)
    {
        _rerequest(ObjectDigest(request->objects(i).hash()));
    }
}

void
ObjectRequester::_deserialize(ObjectDigest const& digest, Slice const& slice)
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
    ++metrics_.searched;

    // TODO: Load in batches.
    auto object = copier_.objectDatabase_.fetchNodeObject(digest);
    if (object)
    {
        ++metrics_.loaded;
        if (metrics_.loaded % 100000 == 0)
        {
            JLOG(copier_.journal_.trace()) << "loaded: " << metrics_.loaded;
            // TODO: Load from disk in parallel, after loading a few hundred
            // with the same requester.
        }
        metrics_.ireceived += requested;
        _deserialize(digest, object->getData());
        return;
    }

    if (!request_)
    {
        request_ = std::make_unique<RequestPtr::element_type>();
        request_->set_type(protocol::TMGetObjectByHash::otUNKNOWN);
    }

    request_->add_objects()->set_hash(digest.begin(), digest.size());
    metrics_.requested += 1 - requested;

    if (request_->objects_size() >= CopyLedger::MAX_OBJECTS_PER_MESSAGE)
    {
        _send();
    }
}

void
ObjectRequester::_send()
{
    assert(request_);
    copier_.send(std::move(request_));
    assert(!request_);
}

ObjectRequester::~ObjectRequester()
{
    if (request_)
    {
        _send();
    }
}

}  // namespace sync
}  // namespace ripple
