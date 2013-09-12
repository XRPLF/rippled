//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace Validators
{

class Tests : public UnitTest
{
public:
   enum
    {
        numberOfTestValidators = 1000
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
            return "Test";
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
                info.key = Validators::PublicKey::createFromInteger (i);
                result.list.add (info);
            }

            return result;
        }

        String m_name;
        std::size_t m_start;
        std::size_t m_end;
    };

    //--------------------------------------------------------------------------

    void addSources (Logic& logic)
    {
#if 0
        logic.addSource (new TestSource ("source 1",    0, 1000));
        logic.addSource (new TestSource ("source 2",  200, 1500));
        logic.addSource (new TestSource ("source 3",  500, 2000));
        logic.addSource (new TestSource ("source 4",  750, 2200));
        logic.addSource (new TestSource ("source 5", 1500, 3200));
#else
        logic.addSource (new TestSource ("source 1",    0, 1));
#endif
    }

    void testLogic ()
    {
        beginTestCase ("logic");

        Logic logic;
        addSources (logic);

        NoOpCancelCallback cancelCallback;
        logic.checkSources (cancelCallback);

        ChosenList::Ptr list (logic.getChosenList ());

        pass ();
    }

    void runTest ()
    {
        testLogic ();
    }

    Tests () : UnitTest ("Validators", "ripple", runManual)
    {
    }
};

static Tests tests;

}
