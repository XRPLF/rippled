//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace ripple {
namespace Validators {

class Tests : public UnitTest
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
        TestSource (String const& name, uint32 start, uint32 end)
            : m_name (name)
            , m_start (start)
            , m_end (end)
        {
        }

        String name ()
        {
            return uniqueID ();
        }

        String uniqueID ()
        {
            return String ("Test,") + m_name + "," +
                String::fromNumber (m_start) + "," +
                String::fromNumber (m_end);
        }

        String createParam ()
        {
            return String::empty;
        }

        Result fetch (CancelCallback& cancel, Journal)
        {
            Result result;

            result.success = true;
            result.message = String::empty;
            result.list.ensureStorageAllocated (numberOfTestValidators);

            for (uint32 i = m_start ; i < m_end; ++i)
            {
                Info info;
                info.publicKey = Validators::PublicKey::createFromInteger (i);
                info.label = String::fromNumber (i);
                result.list.add (info);
            }

            return result;
        }

        String m_name;
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
        for (int i = 1; i <= numberofTestSources; ++i)
        {
            String const name (String::fromNumber (i));
            uint32 const start = random().nextInt (numberOfTestValidators);
            uint32 const end   = start + random().nextInt (numberOfTestValidators);
            logic.add (new TestSource (name, start, end));
        }
    }

    void testLogic ()
    {
        beginTestCase ("logic");

        //TestStore store;
        StoreSqdb storage;

        File const file (
            File::getSpecialLocation (
                File::userDocumentsDirectory).getChildFile (
                    "validators-test.sqlite"));

        // Can't call this 'error' because of ADL and Journal::error
        Error err (storage.open (file));

        unexpected (err, err.what());

        Logic logic (storage, Journal ());
        logic.load ();

        addSources (logic);

        NoOpCancelCallback cancelCallback;
        logic.check (cancelCallback);

        ChosenList::Ptr list (logic.getChosen ());

        pass ();
    }

    void runTest ()
    {
        // We need to use the same seed so we create the
        // same IDs for the set of TestSource objects.
        //
        int64 const seedValue = 10;
        random().setSeed (seedValue);

        testLogic ();
    }

    Tests () : UnitTest ("Validators", "ripple", runManual)
    {
    }
};

static Tests tests;

}
}
