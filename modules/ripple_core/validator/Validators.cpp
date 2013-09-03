//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

/*

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
  I've cut 2 of the 6 active client-facing servers to hyper. Since then, we've
  had 5 spinouts on 3 servers, none of them on the 2 I've cut over. But they
  are also the most recently restarted servers, so it's not a 100% fair test.

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

Nouns

  Validator
    - Signs ledgers and participate in consensus
    - Fields
      * Public key
      * Friendly name
      * Jurisdiction 
      * Org type: profit, nonprofit, "profit/gateway"
    - Metadata
      * Visible on the network?
      * On the consensus ledger?
      * Percentage of recent participation in consensus
      * Frequency of stalling the consensus process

  ValidatorSource
    - Abstract
    - Provides a list of Validator

  ValidatorList
    - Essentially an array of Validator

  ValidatorSourceTrustedUri
    - ValidatorSource which uses HTTPS and a predefined URI
    - Domain owner is responsible for removing bad validators

  ValidatorSourceTrustedUri::List
    - Essentially an array of ValidatorSourceTrustedUri
    - Can be read from a file

  LocalFileValidatorSource
    - ValidatorSource which reads information from a local file.

  TrustedUriList // A copy of this ships with the app
  * has a KnownValidators

  KnownValidators
  * A series of KnownValidator that comes from a TrustedUri
  * Persistent storage has a timestamp

  RankedValidators
  * Created as the union of all KnownValidators with "weight" being the
  number of appearances.

  ChosenValidators
  * Result of the algorithm that chooses a random subset of RankedKnownValidators
  * "local health" percentage is the percent of validations from this list that
  you've seen recently. And have they been behaving.
*/

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

    struct TestSource : Validators::Source
    {
        TestSource (String const& name, uint32 start, uint32 end)
            : m_name (name)
            , m_start (start)
            , m_end (end)
        {
        }

        Array <Info> fetch (CancelCallback& cancel)
        {
            Array <Info> list;
            list.ensureStorageAllocated (numberOfTestValidators);
            for (uint32 i = m_start ; i < m_end; ++i)
            {
                Info info;
                info.key = Validators::KeyType::createFromInteger (i);
                list.add (info);
            }
            return list;
        }

        String m_name;
        std::size_t m_start;
        std::size_t m_end;
    };

    //--------------------------------------------------------------------------

    void addSources (ValidatorsImp::Logic& logic)
    {
        logic.addSource (new TestSource ("source 1",    0, 1000));
        logic.addSource (new TestSource ("source 2",  200, 1500));
        logic.addSource (new TestSource ("source 3",  500, 2000));
        logic.addSource (new TestSource ("source 4",  750, 2200));
        logic.addSource (new TestSource ("source 5", 1500, 3200));
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

