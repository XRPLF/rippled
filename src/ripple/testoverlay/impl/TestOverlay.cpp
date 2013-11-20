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

namespace TestOverlay
{

class Tests : public UnitTest
{
public:
    template <class Config>
    class SeenState : public StateBase <Config>
    {
    public:
        SeenState ()
            : m_seen (0)
        {
        }

        void increment ()
        {
            ++m_seen;
        }

        int seen () const
        {
            return m_seen;
        }

    private:
        int m_seen;
    };

    //--------------------------------------------------------------------------

    template <class Config>
    class PeerLogic : public PeerLogicBase <Config>
    {
    public:
        typedef PeerLogicBase <Config> Base;
        typedef typename Config::Payload    Payload;
        typedef typename Base::Connection   Connection;
        typedef typename Base::Peer         Peer;
        typedef typename Base::Message      Message;
        typedef typename Config::SizeType   SizeType;

        explicit PeerLogic (Peer& peer)
            : PeerLogicBase <Config> (peer)
        {
        }

        ~PeerLogic ()
        {
        }

        void step ()
        {
            if (this->peer().id () == 1)
            {
                if (this->peer().network().steps() == 0)
                {
                    this->peer().network().state().increment();
                    this->peer().send_all (Payload (1));
                }
            }
        }

        void receive (Connection const& c, Message const& m)
        {
            if (this->peer().id () != 1)
            {
                this->peer().network().state().increment();
                this->peer().send_all_if (Message (m.id(),
                    m.payload().withHop ()),
                        typename Connection::IsNotPeer (c.peer()));
            }
        }
    };

    //--------------------------------------------------------------------------

    struct Params : ConfigType <
        Params,
        SeenState,
        PeerLogic
    >
    {
        typedef PremadeInitPolicy <250, 3> InitPolicy;
    };

    typedef Params::Network Network;

    //--------------------------------------------------------------------------

    void testCreation ()
    {
        beginTestCase ("create");

        Network network;

        Results result;
        for (int i = 0; result.received < 249 && i < 100; ++i)
        {
            String s =
                String ("step #") + String::fromNumber (
                network.steps()) + " ";
            result += network.step ();
            s << result.toString ();
            logMessage (s);
        }

        int const seen (network.state().seen());

        String s = "Seen = " + String::fromNumber (seen);
        logMessage (s);
        pass ();
    }

    void runTest ()
    {
        testCreation ();
    }

    Tests () : UnitTest ("TestOverlay", "ripple", runManual)
    {
    }
};

static Tests tests;

}
