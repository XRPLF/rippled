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

#include <boost/algorithm/string.hpp>

namespace ripple {

class ProofOfWorkFactoryImp
    : public ProofOfWorkFactory
    , public beast::LeakChecked <ProofOfWorkFactoryImp>
{
public:
    typedef boost::bimap< boost::bimaps::multiset_of<time_t>,
        boost::bimaps::unordered_set_of<uint256> > powMap_t;

    typedef powMap_t::value_type    powMap_vt;

    //--------------------------------------------------------------------------

    ProofOfWorkFactoryImp ()
        : mValidTime (180)
    {
        setDifficulty (1);
        RandomNumbers::getInstance ().fillBytes (mSecret.begin (), mSecret.size ());
    }

    //--------------------------------------------------------------------------

    enum
    {
        numPowEntries = kMaxDifficulty + 1
    };

    struct PowEntry
    {
        const char* target;
        int iterations;
    };

    typedef std::vector <PowEntry> PowEntries;

    static PowEntries const& getPowEntries ()
    {
        struct StaticPowEntries
        {
            StaticPowEntries ()
            {
                // VFALCO TODO Make this array know its own size.
                //
                PowEntry entries [numPowEntries] =
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

                data.reserve (numPowEntries);

                for (int i = 0; i < numPowEntries; ++i)
                    data.push_back (entries [i]);
            }

            std::vector <PowEntry> data;
        };

        static StaticPowEntries list;

        return list.data;
    }

    //--------------------------------------------------------------------------

    static int getPowEntry (uint256 const& target, int iterations)
    {
        PowEntries const& entries (getPowEntries ());

        for (int i = 0; i < numPowEntries; ++i)
        {
            if (entries [i].iterations == iterations)
            {
                uint256 t;
                t.SetHex (entries [i].target);

                if (t == target)
                    return i;
            }
        }

        return -1;
    }

    //--------------------------------------------------------------------------

    ProofOfWork getProof ()
    {
        // challenge - target - iterations - time - validator
        static boost::format f ("%s-%s-%d-%d");

        int now = static_cast<int> (time (nullptr) / 4);

        uint256 challenge;
        RandomNumbers::getInstance ().fillBytes (challenge.begin (), challenge.size ());

        ScopedLockType sl (mLock);

        std::string s = boost::str (boost::format (f) % to_string (challenge) % 
            to_string (mTarget) % mIterations % now);
        std::string c = to_string (mSecret) + s;
        s += "-" + to_string (Serializer::getSHA512Half (c));

        return ProofOfWork (s, mIterations, challenge, mTarget);
    }

    //--------------------------------------------------------------------------

    PowResult checkProof (const std::string& token, uint256 const& solution)
    {
        // VFALCO COmmented this out because Dave said it wasn't used
        //        and also we dont have the lexicalCast from a vector of strings to a time_t

        // challenge - target - iterations - time - validator

        std::vector<std::string> fields;
        boost::split (fields, token, boost::algorithm::is_any_of ("-"));

        if (fields.size () != 5)
        {
            WriteLog (lsDEBUG, ProofOfWork) << "PoW " << token << " is corrupt";
            return powCORRUPT;
        }

        std::string v = to_string (mSecret) + fields[0] + "-" + 
                        fields[1] + "-" + fields[2] + "-" + fields[3];

        if (fields[4] != to_string (Serializer::getSHA512Half (v)))
        {
            WriteLog (lsDEBUG, ProofOfWork) << "PoW " << token << " has a bad token";
            return powCORRUPT;
        }

        uint256 challenge, target;
        challenge.SetHex (fields[0]);
        target.SetHex (fields[1]);

        time_t t;
    #if 0
        // Broken with lexicalCast<> changes
        t = beast::lexicalCast <time_t> (fields[3]);
    #else
        t = static_cast <time_t> (beast::lexicalCast <std::uint64_t> (fields [3]));
    #endif

        time_t now = time (nullptr);

        int iterations = beast::lexicalCast <int> (fields[2]);

        {
            ScopedLockType sl (mLock);

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
            ScopedLockType sl (mLock);

            if (!mSolvedChallenges.insert (powMap_vt (now, challenge)).second)
            {
                WriteLog (lsDEBUG, ProofOfWork) << "PoW " << token << " has been reused";
                return powREUSED;
            }
        }

        return powOK;
    }

    //--------------------------------------------------------------------------

    void sweep ()
    {
        time_t expire = time (nullptr) - mValidTime;

        ScopedLockType sl (mLock);

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

    //--------------------------------------------------------------------------

    void loadHigh ()
    {
        time_t now = time (nullptr);

        ScopedLockType sl (mLock);

        if (mLastDifficultyChange == now)
            return;

        if (mPowEntry == 30)
            return;

        ++mPowEntry;
        mLastDifficultyChange = now;
    }

    //--------------------------------------------------------------------------

    void loadLow ()
    {
        time_t now = time (nullptr);

        ScopedLockType sl (mLock);

        if (mLastDifficultyChange == now)
            return;

        if (mPowEntry == 0)
            return;

        --mPowEntry;
        mLastDifficultyChange = now;
    }

    //--------------------------------------------------------------------------

    void setDifficulty (int i)
    {
        assert ((i >= 0) && (i <= kMaxDifficulty));
        time_t now = time (nullptr);

        ScopedLockType sl (mLock);
        mPowEntry = i;
        PowEntries const& entries (getPowEntries ());
        mIterations = entries [i].iterations;
        mTarget.SetHex (entries [i].target);
        mLastDifficultyChange = now;
    }

    //--------------------------------------------------------------------------

    std::uint64_t getDifficulty ()
    {
        return ProofOfWork::getDifficulty (mTarget, mIterations);
    }

    uint256 const& getSecret () const
    {
        return mSecret;
    }

    void setSecret (uint256 const& secret)
    {
        mSecret = secret;
    }

private:
    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    uint256      mSecret;
    int          mIterations;
    uint256      mTarget;
    time_t       mLastDifficultyChange;
    int          mValidTime;
    int          mPowEntry;

    powMap_t     mSolvedChallenges;
};

//------------------------------------------------------------------------------

ProofOfWorkFactory* ProofOfWorkFactory::New ()
{
    return new ProofOfWorkFactoryImp;
}

//------------------------------------------------------------------------------

class ProofOfWork_test : public beast::unit_test::suite
{
public:
    void run ()
    {
        using namespace ripple;

        ProofOfWorkFactoryImp gen;
        ProofOfWork pow = gen.getProof ();

        beast::String s;
        
        s << "solve difficulty " << beast::String (pow.getDifficulty ());

        uint256 solution = pow.solve (16777216);

        expect (! solution.isZero (), "Should be solved");

        expect (pow.checkSolution (solution), "Should be checked");

        // Why is this emitted?
        //WriteLog (lsDEBUG, ProofOfWork) << "A bad nonce error is expected";

        PowResult r = gen.checkProof (pow.getToken (), uint256 ());

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

BEAST_DEFINE_TESTSUITE_MANUAL(ProofOfWork,ripple_app,ripple);

} // ripple
