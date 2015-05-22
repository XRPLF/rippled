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

#ifndef RIPPLE_NODESTORE_BASE_H_INCLUDED
#define RIPPLE_NODESTORE_BASE_H_INCLUDED

#include <ripple/nodestore/Database.h>
#include <ripple/basics/StringUtilities.h>
#include <beast/unit_test/suite.h>
#include <beast/module/core/maths/Random.h>
#include <boost/algorithm/string.hpp>
#include <iomanip>

namespace ripple {
namespace NodeStore {

/** Binary function that satisfies the strict-weak-ordering requirement.

    This compares the hashes of both objects and returns true if
    the first hash is considered to go before the second.

    @see std::sort
*/
struct LessThan
{
    bool
    operator()(
        std::shared_ptr<NodeObject> const& lhs,
            std::shared_ptr<NodeObject> const& rhs) const noexcept
    {
        return lhs->getHash () < rhs->getHash ();
    }
};

/** Returns `true` if objects are identical. */
inline
bool isSame (std::shared_ptr<NodeObject> const& lhs,
    std::shared_ptr<NodeObject> const& rhs)
{
    return
        (lhs->getType() == rhs->getType()) &&
        (lhs->getHash() == rhs->getHash()) &&
        (lhs->getData() == rhs->getData());
}

// Some common code for the unit tests
//
class TestBase : public beast::unit_test::suite
{
public:
    // Tunable parameters
    //
    enum
    {
        maxPayloadBytes = 2000,
        numObjectsToTest = 2000
    };

    // Creates predictable objects
    class PredictableObjectFactory
    {
    public:
        explicit PredictableObjectFactory (std::int64_t seedValue)
            : r (seedValue)
        {
        }

        std::shared_ptr<NodeObject> createObject ()
        {
            NodeObjectType type;
            switch (r.nextInt (4))
            {
            case 0: type = hotLEDGER; break;
            case 2: type = hotACCOUNT_NODE; break;
            case 3: type = hotTRANSACTION_NODE; break;
            default:
            case 1: // was hotTRANSACTION
                type = hotUNKNOWN;
                break;
            };

            uint256 hash;
            r.fillBitsRandomly (hash.begin (), hash.size ());

            int const payloadBytes = 1 + r.nextInt (maxPayloadBytes);

            Blob data (payloadBytes);

            r.fillBitsRandomly (data.data (), payloadBytes);

            return NodeObject::createObject(type, std::move(data), hash);
        }

    private:
        beast::Random r;
    };

public:
 // Create a predictable batch of objects
 static void createPredictableBatch(Batch& batch, int numObjects,
                                    std::int64_t seedValue) {
        batch.reserve (numObjects);

        PredictableObjectFactory factory (seedValue);

        for (int i = 0; i < numObjects; ++i)
            batch.push_back (factory.createObject ());
    }

    // Compare two batches for equality
    static bool areBatchesEqual (Batch const& lhs, Batch const& rhs)
    {
        bool result = true;

        if (lhs.size () == rhs.size ())
        {
            for (int i = 0; i < lhs.size (); ++i)
            {
                if (! isSame(lhs[i], rhs[i]))
                {
                    result = false;
                    break;
                }
            }
        }
        else
        {
            result = false;
        }

        return result;
    }

    // Store a batch in a backend
    void storeBatch (Backend& backend, Batch const& batch)
    {
        for (int i = 0; i < batch.size (); ++i)
        {
            backend.store (batch [i]);
        }
    }

    // Get a copy of a batch in a backend
    void fetchCopyOfBatch (Backend& backend, Batch* pCopy, Batch const& batch)
    {
        pCopy->clear ();
        pCopy->reserve (batch.size ());

        for (int i = 0; i < batch.size (); ++i)
        {
            std::shared_ptr<NodeObject> object;

            Status const status = backend.fetch (
                batch [i]->getHash ().cbegin (), &object);

            expect (status == ok, "Should be ok");

            if (status == ok)
            {
                expect (object != nullptr, "Should not be null");

                pCopy->push_back (object);
            }
        }
    }

    void fetchMissing(Backend& backend, Batch const& batch)
    {
        for (int i = 0; i < batch.size (); ++i)
        {
            std::shared_ptr<NodeObject> object;

            Status const status = backend.fetch (
                batch [i]->getHash ().cbegin (), &object);

            expect (status == notFound, "Should be notFound");
        }
    }

    // Store all objects in a batch
    static void storeBatch (Database& db, Batch const& batch)
    {
        for (int i = 0; i < batch.size (); ++i)
        {
            std::shared_ptr<NodeObject> const object (batch [i]);

            Blob data (object->getData ());

            db.store (object->getType (),
                      std::move (data),
                      object->getHash ());
        }
    }

    // Fetch all the hashes in one batch, into another batch.
    static void fetchCopyOfBatch (Database& db,
                                  Batch* pCopy,
                                  Batch const& batch)
    {
        pCopy->clear ();
        pCopy->reserve (batch.size ());

        for (int i = 0; i < batch.size (); ++i)
        {
            std::shared_ptr<NodeObject> object = db.fetch (batch [i]->getHash ());

            if (object != nullptr)
                pCopy->push_back (object);
        }
    }
};

}
}

#endif
