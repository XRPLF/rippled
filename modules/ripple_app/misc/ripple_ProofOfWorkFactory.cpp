//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

ProofOfWorkFactory::ProofOfWorkFactory () :  mValidTime (180)
{
    setDifficulty (1);
    RandomNumbers::getInstance ().fillBytes (mSecret.begin (), mSecret.size ());
}

ProofOfWork ProofOfWorkFactory::getProof ()
{
    // challenge - target - iterations - time - validator
    static boost::format f ("%s-%s-%d-%d");

    int now = static_cast<int> (time (NULL) / 4);

    uint256 challenge;
    RandomNumbers::getInstance ().fillBytes (challenge.begin (), challenge.size ());

    boost::mutex::scoped_lock sl (mLock);

    std::string s = boost::str (boost::format (f) % challenge.GetHex () % mTarget.GetHex () % mIterations % now);
    std::string c = mSecret.GetHex () + s;
    s += "-" + Serializer::getSHA512Half (c).GetHex ();

    return ProofOfWork (s, mIterations, challenge, mTarget);
}

POWResult ProofOfWorkFactory::checkProof (const std::string& token, uint256 const& solution)
{
    // challenge - target - iterations - time - validator

    std::vector<std::string> fields;
    boost::split (fields, token, boost::algorithm::is_any_of ("-"));

    if (fields.size () != 5)
    {
        WriteLog (lsDEBUG, ProofOfWork) << "PoW " << token << " is corrupt";
        return powCORRUPT;
    }

    std::string v = mSecret.GetHex () + fields[0] + "-" + fields[1] + "-" + fields[2] + "-" + fields[3];

    if (fields[4] != Serializer::getSHA512Half (v).GetHex ())
    {
        WriteLog (lsDEBUG, ProofOfWork) << "PoW " << token << " has a bad token";
        return powCORRUPT;
    }

    uint256 challenge, target;
    challenge.SetHex (fields[0]);
    target.SetHex (fields[1]);

    time_t t = lexical_cast_s<time_t> (fields[3]);
    time_t now = time (NULL);

    int iterations = lexical_cast_s<int> (fields[2]);

    {
        boost::mutex::scoped_lock sl (mLock);

        if ((t * 4) > (now + mValidTime))
        {
            WriteLog (lsDEBUG, ProofOfWork) << "PoW " << token << " has expired";
            return powEXPIRED;
        }

        if (((iterations != mIterations) || (target != mTarget)) && getPowEntry (target, iterations) < (mPowEntry - 2))
        {
            // difficulty has increased more than two times since PoW requested
            WriteLog (lsINFO, ProofOfWork) << "Difficulty has increased since PoW requested";
            return powTOOEASY;
        }
    }

    ProofOfWork pow (token, iterations, challenge, target);

    if (!pow.checkSolution (solution))
    {
        WriteLog (lsDEBUG, ProofOfWork) << "PoW " << token << " has a bad nonce";
        return powBADNONCE;
    }

    {
        boost::mutex::scoped_lock sl (mLock);

        if (!mSolvedChallenges.insert (powMap_vt (now, challenge)).second)
        {
            WriteLog (lsDEBUG, ProofOfWork) << "PoW " << token << " has been reused";
            return powREUSED;
        }
    }

    return powOK;
}

void ProofOfWorkFactory::sweep ()
{
    time_t expire = time (NULL) - mValidTime;

    boost::mutex::scoped_lock sl (mLock);

    do
    {
        powMap_t::left_map::iterator it = mSolvedChallenges.left.begin ();

        if (it == mSolvedChallenges.left.end ())
            return;

        if (it->first >= expire)
            return;

        mSolvedChallenges.left.erase (it);
    }
    while (1);
}

void ProofOfWorkFactory::loadHigh ()
{
    time_t now = time (NULL);

    boost::mutex::scoped_lock sl (mLock);

    if (mLastDifficultyChange == now)
        return;

    if (mPowEntry == 30)
        return;

    ++mPowEntry;
    mLastDifficultyChange = now;
}

void ProofOfWorkFactory::loadLow ()
{
    time_t now = time (NULL);

    boost::mutex::scoped_lock sl (mLock);

    if (mLastDifficultyChange == now)
        return;

    if (mPowEntry == 0)
        return;

    --mPowEntry;
    mLastDifficultyChange = now;
}

struct PowEntry
{
    const char* target;
    int iterations;
};

PowEntry PowEntries[ProofOfWork::sMaxDifficulty + 1] =
{
    //   target                                                             iterations  hashes        memory
    { "0CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 65536 }, // 1451874,      2 MB
    { "0CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 98304 }, // 2177811,      3 MB
    { "07FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 98304 }, // 3538944,      3 MB
    { "0CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 196608}, // 4355623,      6 MB

    { "07FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 131072}, // 4718592,      4 MB
    { "0CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 5807497,      8 MB
    { "07FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 196608}, // 7077888,      6 MB
    { "07FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 9437184,      8 MB

    { "07FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 14155776,     12MB
    { "03FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 28311552,     12MB
    { "00CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 92919965,     8 MB
    { "00CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 139379948,    12MB

    { "007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 150994944,    8 MB
    { "007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 226492416,    12MB
    { "000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 49152 }, // 278759896,    1.5MB
    { "003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 301989888,    8 MB

    { "003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 452984832,    12MB
    { "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 98304 }, // 905969664,    3 MB
    { "000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 196608}, // 1115039586,   6 MB
    { "000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 1486719448    8 MB

    { "000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 2230079172    12MB
    { "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 2415919104,   8 MB
    { "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 3623878656,   12MB
    { "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 7247757312,   12MB

    { "0000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 23787511177,  8 MB
    { "0000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 35681266766,  12MB
    { "00003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 131072}, // 38654705664,  4 MB
    { "00007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 38654705664,  8 MB

    { "00003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 196608}, // 57982058496,  6 MB
    { "00007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 57982058496,  12MB
    { "00003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 77309411328,  8 MB
};

int ProofOfWorkFactory::getPowEntry (uint256 const& target, int iterations)
{
    for (int i = 0; i < 31; ++i)
        if (PowEntries[i].iterations == iterations)
        {
            uint256 t;
            t.SetHex (PowEntries[i].target);

            if (t == target)
                return i;
        }

    return -1;
}

void ProofOfWorkFactory::setDifficulty (int i)
{
    assert ((i >= 0) && (i <= ProofOfWork::sMaxDifficulty));
    time_t now = time (NULL);

    boost::mutex::scoped_lock sl (mLock);
    mPowEntry = i;
    mIterations = PowEntries[i].iterations;
    mTarget.SetHex (PowEntries[i].target);
    mLastDifficultyChange = now;
}

IProofOfWorkFactory* IProofOfWorkFactory::New ()
{
    return new ProofOfWorkFactory;
}

//------------------------------------------------------------------------------

class ProofOfWorkTests : public UnitTest
{
public:
    ProofOfWorkTests () : UnitTest ("ProofOfWork", "ripple", UnitTest::runManual)
    {
    }

    void runTest ()
    {
        using namespace ripple;

        ProofOfWorkFactory gen;
        ProofOfWork pow = gen.getProof ();

        String s;
        
        s << "solve difficulty " << String (pow.getDifficulty ());
        beginTest ("solve");

        uint256 solution = pow.solve (16777216);

        expect (! solution.isZero (), "Should be solved");

        expect (pow.checkSolution (solution), "Should be checked");

        // Why is this emitted?
        //WriteLog (lsDEBUG, ProofOfWork) << "A bad nonce error is expected";

        POWResult r = gen.checkProof (pow.getToken (), uint256 ());

        expect (r == powBADNONCE, "Should show bad nonce for empty solution");

        expect (gen.checkProof (pow.getToken (), solution) == powOK, "Solution should check with issuer");

        //WriteLog (lsDEBUG, ProofOfWork) << "A reused nonce error is expected";

        expect (gen.checkProof (pow.getToken (), solution) == powREUSED, "Reuse solution should be detected");

    #ifdef SOLVE_POWS

        for (int i = 0; i < 12; ++i)
        {
            gen.setDifficulty (i);
            ProofOfWork pow = gen.getProof ();
            WriteLog (lsINFO, ProofOfWork) << "Level: " << i << ", Estimated difficulty: " << pow.getDifficulty ();
            uint256 solution = pow.solve (131072);

            if (solution.isZero ())
            {
                //WriteLog (lsINFO, ProofOfWork) << "Giving up";
            }
            else
            {
                //WriteLog (lsINFO, ProofOfWork) << "Solution found";

                if (gen.checkProof (pow.getToken (), solution) != powOK)
                {
                    //WriteLog (lsFATAL, ProofOfWork) << "Solution fails";
                }
            }
        }

    #endif
    }
};

static ProofOfWorkTests proofOfWorkTests;
