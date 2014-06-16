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

#include <beast/unit_test/suite.h>

namespace ripple {
namespace Validators {

class Logic_test : public beast::unit_test::suite
{
public:
    enum
    {
        numberOfTestValidators = 1000,
        numberofTestSources = 50
    };

    //--------------------------------------------------------------------------

    struct Payload
    {
        Payload ()
        {
        }
    };

    template <class Config>
    class PeerLogic : public TestOverlay::PeerLogicBase <Config>
    {
    public:
        typedef TestOverlay::PeerLogicBase <Config> Base;
        typedef typename Config::Payload    Payload;
        typedef typename Base::Connection   Connection;
        typedef typename Base::Peer         Peer;
        typedef typename Base::Message      Message;
        typedef typename Config::SizeType   SizeType;

        explicit PeerLogic (Peer& peer)
            : TestOverlay::PeerLogicBase <Config> (peer)
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

    struct Params : TestOverlay::ConfigType <
        Params,
        TestOverlay::StateBase,
        PeerLogic
    >
    {
        typedef TestOverlay::PremadeInitPolicy <250, 3> InitPolicy;
    };

    typedef Params::Network Network;

    //--------------------------------------------------------------------------

    struct TestSource : Source
    {
        TestSource (beast::String const& name, std::uint32_t start, std::uint32_t end)
            : m_name (name)
            , m_start (start)
            , m_end (end)
        {
        }

        std::string to_string () const
        {
            return uniqueID().toStdString();
        }

        beast::String uniqueID () const
        {
            using beast::String;
            return String ("Test,") + m_name + "," +
                String::fromNumber (m_start) + "," +
                String::fromNumber (m_end);
        }

        beast::String createParam ()
        {
            return beast::String::empty;
        }

        void fetch (Results& results, beast::Journal)
        {
            results.success = true;
            results.message = beast::String::empty;
            results.list.reserve (numberOfTestValidators);

            for (std::uint32_t i = m_start ; i < m_end; ++i)
            {
                Item item;;
                item.publicKey = RipplePublicKey::createFromInteger (i);
                item.label = beast::String::fromNumber (i);
                results.list.push_back (item);
            }
        }

        beast::String m_name;
        std::size_t m_start;
        std::size_t m_end;
    };

    //--------------------------------------------------------------------------

    class TestStore : public Store
    {
    public:
        TestStore ()
        {
        }

        ~TestStore ()
        {
        }

        void insertSourceDesc (SourceDesc& desc)
        {
        }

        void updateSourceDesc (SourceDesc& desc)
        {
        }

        void updateSourceDescInfo (SourceDesc& desc)
        {
        }
    };

    //--------------------------------------------------------------------------

    void addSources (Logic& logic)
    {
        beast::Random r;
        for (int i = 1; i <= numberofTestSources; ++i)
        {
            beast::String const name (beast::String::fromNumber (i));
            std::uint32_t const start = r.nextInt (numberOfTestValidators);
            std::uint32_t const end   = start + r.nextInt (numberOfTestValidators);
            logic.add (new TestSource (name, start, end));
        }
    }

    void testLogic ()
    {
        //TestStore store;
        StoreSqdb storage;

        beast::File const file (
            beast::File::getSpecialLocation (
                beast::File::userDocumentsDirectory).getChildFile (
                    "validators-test.sqlite"));

        // Can't call this 'error' because of ADL and Journal::error
        beast::Error err (storage.open (file));

        expect (! err, err.what());

        Logic logic (storage, beast::Journal ());
        logic.load ();

        addSources (logic);

        logic.fetch_one ();

        ChosenList::Ptr list (logic.getChosen ());

        pass ();
    }

    void
    run ()
    {
        testLogic ();
    }
};

BEAST_DEFINE_TESTSUITE(Logic,validators,ripple);

}
}
