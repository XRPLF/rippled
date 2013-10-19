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
namespace PeerFinder {

class PeerFinderTests : public UnitTest
{
public:


    //--------------------------------------------------------------------------

    // Complete Logic used for tests
    //
    class TestLogic
        : public LogicType <ManualClock>
        , public Callback
        , public Store
        , public Checker
    {
    public:
        Journal m_journal;

        explicit TestLogic (Journal journal)
            : LogicType <ManualClock> (*this, *this, *this, journal)
            , m_journal (journal)
        {
        }

        //
        // Callback
        //

        void sendPeerEndpoints (PeerID const& id,
            std::vector <Endpoint> const& endpoints)
        {
        }

        void connectPeerEndpoints (std::vector <IPAddress> const& list)
        {
        }

        void chargePeerLoadPenalty (PeerID const& id)
        {
        }
      
        //
        // Store
        //

        void loadLegacyEndpoints (std::vector <IPAddress>& list)
        {
        }

        void updateLegacyEndpoints (std::vector <LegacyEndpoint const*> const& list)
        {
        }

        //
        // Checker
        //

        void cancel ()
        {
        }

        void async_test (IPAddress const& address,
            AbstractHandler <void (Result)> handler)
        {
            Checker::Result result;
            result.address = address;
            result.canAccept = false;
            handler (result);
        }
    };

    //--------------------------------------------------------------------------

    void runTest ()
    {
        beginTestCase ("logic");

        TestLogic logic (journal());

        pass ();
    }

    PeerFinderTests () : UnitTest ("PeerFinder", "ripple", runManual)
    {
    }
};

static PeerFinderTests peerFinderTests;

}
}
