//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/*

Information to track:

- Percentage of validations that the validator has signed
- Number of validations the validator signed that never got accepted


- Target number for Chosen
- Pseudo-randomly choose a subset from Chosen





Goal:

  Provide the listener with a ValidatorList.
  - This forms the UNL

Task:

  fetch ValidatorInfo array from a source

  - We have the old one and the new one, compute the following:

    * unchanged validators list
    * new validators list
    * removed validators list

  - From the unchanged / new / removed, figure out what to do.

Two important questions:

- Are there any validators in my ChosenValidators that I dont want
  * For example, they have dropped off all the trusted lists

- Do I have enough?

--------------------------------------------------------------------------------
ChosenValidators
--------------------------------------------------------------------------------

David:
  Maybe OC should have a URL that you can query to get the latest list of URI's
  for OC-approved organzations that publish lists of validators. The server and
  client can ship with that master trust URL and also the list of URI's at the
  time it's released, in case for some reason it can't pull from OC. That would
  make the default installation safe even against major changes in the
  organizations that publish validator lists.

  The difference is that if an organization that provides lists of validators
  goes rogue, administrators don't have to act.

TODO:
  Write up from end-user perspective on the deployment and administration
  of this feature, on the wiki. "DRAFT" or "PROPOSE" to mark it as provisional.
  Template: https://ripple.com/wiki/Federation_protocol
  - What to do if you're a publisher of ValidatorList
  - What to do if you're a rippled administrator
  - Overview of how ChosenValidators works

Goals:
  Make default configuration of rippled secure.
    * Ship with TrustedUriList
    * Also have a preset RankedValidators
  Eliminate administrative burden of maintaining
  Produce the ChosenValidators list.
  Allow quantitative analysis of network health.

What determines that a validator is good?
  - Are they present (i.e. sending validations)
  - Are they on the consensus ledger
  - What percentage of consensus rounds do they participate in
  - Are they stalling consensus
    * Measurements of constructive/destructive behavior is
      calculated in units of percentage of ledgers for which
      the behavior is measured.
*/

//------------------------------------------------------------------------------

Validators::Source::Result::Result ()
    : success (false)
    , message ("uninitialized")
{
}

void Validators::Source::Result::swapWith (Result& other)
{
    std::swap (success, other.success);
    std::swap (message, other.message);
    list.swapWith (other.list);
}

//------------------------------------------------------------------------------

Validators* Validators::New ()
{
    return new ValidatorsImp (nullptr);
}

//------------------------------------------------------------------------------

class ValidatorsTests : public UnitTest
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

    struct TestSource : Validators::Source
    {
        TestSource (String const& name, uint32 start, uint32 end)
            : m_name (name)
            , m_start (start)
            , m_end (end)
        {
        }

        Result fetch (CancelCallback& cancel)
        {
            Result result;

            result.success = true;
            result.message = String::empty;
            result.list.ensureStorageAllocated (numberOfTestValidators);

            for (uint32 i = m_start ; i < m_end; ++i)
            {
                Info info;
                info.key = Validators::KeyType::createFromInteger (i);
                result.list.add (info);
            }

            return result;
        }

        String m_name;
        std::size_t m_start;
        std::size_t m_end;
    };

    //--------------------------------------------------------------------------

    void addSources (ValidatorsImp::Logic& logic)
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

        ValidatorsImp::Logic logic;
        addSources (logic);

        ValidatorsImp::NoOpCancelCallback cancelCallback;
        logic.checkSources (cancelCallback);

        ValidatorsImp::ChosenList::Ptr list (logic.getChosenList ());

        pass ();
    }

    void runTest ()
    {
        testLogic ();
    }

    ValidatorsTests () : UnitTest ("Validators", "ripple", runManual)
    {
    }
};

static ValidatorsTests validatorsTests;

