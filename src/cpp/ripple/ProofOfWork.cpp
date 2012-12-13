#include "ProofOfWork.h"

#include <string>

#include <boost/test/unit_test.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include <openssl/rand.h>

#include "Serializer.h"
#include "Log.h"

SETUP_LOG();

const uint256 ProofOfWork::sMinTarget("00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
const int ProofOfWork::sMaxIterations(1 << 23);

bool ProofOfWork::isValid() const
{
	if ((mIterations <= sMaxIterations) && (mTarget >= sMinTarget))
		return true;
	cLog(lsWARNING) << "Invalid PoW: " << mIterations << ", " << mTarget;
	return false;
}

uint64 ProofOfWork::getDifficulty(const uint256& target, int iterations)
{ // calculate the approximate number of hashes required to solve this proof of work
	if ((iterations > sMaxIterations) || (target < sMinTarget))
	{
		cLog(lsINFO) << "Iterations:" << iterations;
		cLog(lsINFO) << "MaxIterat: " << sMaxIterations;
		cLog(lsINFO) << "Target:    " << target;
		cLog(lsINFO) << "MinTarget: " << sMinTarget;
		throw std::runtime_error("invalid proof of work target/iteration");
	}

	// more iterations means more hashes per iteration but also a larger final hash
	uint64 difficulty = iterations + (iterations / 8);

	// Multiply the number of hashes needed by 256 for each leading zero byte in the difficulty
	const unsigned char *ptr = target.begin();
	while (*ptr == 0)
	{
		difficulty *= 256;
		++ptr;
	}
	difficulty = (difficulty * 256) / (*ptr + 1);

	return difficulty;
}

static uint256 getSHA512Half(const std::vector<uint256>& vec)
{
	return Serializer::getSHA512Half(vec.front().begin(), vec.size() * (256 / 8));
}

uint256 ProofOfWork::solve(int maxIterations) const
{
	if (!isValid())
		throw std::runtime_error("invalid proof of work target/iteration");

	uint256 nonce;
	getRand(nonce.begin(), nonce.size());

	std::vector<uint256> buf2;
	buf2.resize(mIterations);

	std::vector<uint256> buf1;
	buf1.resize(3);
	buf1[0] = mChallenge;

	while (maxIterations > 0)
	{
		buf1[1] = nonce;
		buf1[2].zero();
		for (int i = (mIterations - 1); i >= 0; --i)
		{
			buf1[2] = getSHA512Half(buf1);
			buf2[i] = buf1[2];
		}

		if (getSHA512Half(buf2) <= mTarget)
			return nonce;

		++nonce;
		--maxIterations;
	}
	return uint256();
}

bool ProofOfWork::checkSolution(const uint256& solution) const
{
	if (mIterations > sMaxIterations)
		return false;

	std::vector<uint256> buf1;
	buf1.push_back(mChallenge);
	buf1.push_back(solution);
	buf1.push_back(uint256());

	std::vector<uint256> buf2;
	buf2.resize(mIterations);
	for (int i = (mIterations - 1); i >= 0; --i)
	{
		buf1[2] = getSHA512Half(buf1);
		buf2[i] = buf1[2];
	}
	return getSHA512Half(buf2) <= mTarget;
}

ProofOfWorkGenerator::ProofOfWorkGenerator() :	mValidTime(180)
{
	setDifficulty(1);
	getRand(mSecret.begin(), mSecret.size());
}

ProofOfWork ProofOfWorkGenerator::getProof()
{
	// challenge - target - iterations - time - validator
	static boost::format f("%s-%s-%d-%d");

	int now = static_cast<int>(time(NULL) / 4);

	uint256 challenge;
	getRand(challenge.begin(), challenge.size());

	boost::mutex::scoped_lock sl(mLock);

	std::string s = boost::str(f % challenge.GetHex() % mTarget.GetHex() % mIterations % now);
	std::string c = mSecret.GetHex() + s;
	s += "-" + Serializer::getSHA512Half(c).GetHex();

	return ProofOfWork(s, mIterations, challenge, mTarget);
}

POWResult ProofOfWorkGenerator::checkProof(const std::string& token, const uint256& solution)
{ // challenge - target - iterations - time - validator

	std::vector<std::string> fields;
	boost::split(fields, token, boost::algorithm::is_any_of("-"));
	if (fields.size() != 5)
	{
		cLog(lsDEBUG) << "PoW " << token << " is corrupt";
		return powCORRUPT;
	}

	std::string v = mSecret.GetHex() + fields[0] + "-" + fields[1] + "-" + fields[2] + "-" + fields[3];
	if (fields[4] != Serializer::getSHA512Half(v).GetHex())
	{
		cLog(lsDEBUG) << "PoW " << token << " has a bad token";
		return powCORRUPT;
	}

	uint256 challenge, target;
	challenge.SetHex(fields[0]);
	target.SetHex(fields[1]);

	time_t t = lexical_cast_s<time_t>(fields[3]);
	time_t now = time(NULL);

	int iterations = lexical_cast_s<int>(fields[2]);

	{
		boost::mutex::scoped_lock sl(mLock);
		if ((t * 4) > (now + mValidTime))
		{
			cLog(lsDEBUG) << "PoW " << token << " has expired";
			return powEXPIRED;
		}

		if (((iterations != mIterations) || (target != mTarget)) && getPowEntry(target, iterations) < (mPowEntry - 2))
		{ // difficulty has increased more than two times since PoW requested
			cLog(lsINFO) << "Difficulty has increased since PoW requested";
			return powTOOEASY;
		}
	}

	ProofOfWork pow(token, iterations, challenge, target);
	if (!pow.checkSolution(solution))
	{
		cLog(lsDEBUG) << "PoW " << token << " has a bad nonce";
		return powBADNONCE;
	}

	{
		boost::mutex::scoped_lock sl(mLock);
		if (!mSolvedChallenges.insert(powMap_vt(now, challenge)).second)
		{
			cLog(lsDEBUG) << "PoW " << token << " has been reused";
			return powREUSED;
		}
	}

	return powOK;
}

void ProofOfWorkGenerator::sweep()
{
	time_t expire = time(NULL) - mValidTime;

	boost::mutex::scoped_lock sl(mLock);
	do
	{
		powMap_t::left_map::iterator it = mSolvedChallenges.left.begin();
		if (it == mSolvedChallenges.left.end())
			return;
		if (it->first >= expire)
			return;
		mSolvedChallenges.left.erase(it);
	} while(1);
}

void ProofOfWorkGenerator::loadHigh()
{
	time_t now = time(NULL);

	boost::mutex::scoped_lock sl(mLock);
	if (mLastDifficultyChange == now)
		return;
	if (mPowEntry == 30)
		return;
	++mPowEntry;
	mLastDifficultyChange = now;
}

void ProofOfWorkGenerator::loadLow()
{
	time_t now = time(NULL);

	boost::mutex::scoped_lock sl(mLock);
	if (mLastDifficultyChange == now)
		return;
	if (mPowEntry == 0)
		return;
	--mPowEntry;
	mLastDifficultyChange = now;
}

struct PowEntry
{
	const char *target;
	int iterations;
};

PowEntry PowEntries[31] =
{ //   target                                                             iterations  hashes  		memory
	{ "0CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 65536	}, // 1451874,		2 MB
	{ "0CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 98304	}, // 2177811,		3 MB
	{ "07FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 98304	}, // 3538944,		3 MB
	{ "0CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 196608}, // 4355623,		6 MB

	{ "07FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 131072}, // 4718592,		4 MB
	{ "0CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 5807497,		8 MB
	{ "07FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 196608}, // 7077888,		6 MB
	{ "07FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 9437184,		8 MB

	{ "07FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 14155776,		12MB
	{ "03FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 28311552,		12MB
	{ "00CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 92919965,		8 MB
	{ "00CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 139379948,	12MB

	{ "007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 150994944,	8 MB
	{ "007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 226492416,	12MB
	{ "000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 49152	}, // 278759896,	1.5MB
	{ "003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 301989888,	8 MB

	{ "003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 452984832,	12MB
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 98304	}, // 905969664,	3 MB
	{ "000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 196608}, // 1115039586,	6 MB
	{ "000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 1486719448	8 MB

	{ "000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 2230079172	12MB
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 2415919104,	8 MB
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 3623878656,	12MB
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 7247757312,	12MB

	{ "0000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 23787511177,	8 MB
	{ "0000CFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 35681266766,	12MB
	{ "00003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 131072}, // 38654705664,	4 MB
	{ "00007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 38654705664,	8 MB

	{ "00003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 196608}, // 57982058496,	6 MB
	{ "00007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 393216}, // 57982058496,	12MB
	{ "00003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144}, // 77309411328,	8 MB
};

int ProofOfWorkGenerator::getPowEntry(const uint256& target, int iterations)
{
	for (int i = 0; i < 31; ++i)
		if (PowEntries[i].iterations == iterations)
		{
			uint256 t;
			t.SetHex(PowEntries[i].target);
			if (t == target)
				return i;
		}
	return -1;
}

void ProofOfWorkGenerator::setDifficulty(int i)
{
	assert((i >= 0) && (i <= 30));
	time_t now = time(NULL);

	boost::mutex::scoped_lock sl(mLock);
	mPowEntry = i;
	mIterations = PowEntries[i].iterations;
	mTarget.SetHex(PowEntries[i].target);
	mLastDifficultyChange = now;
}

BOOST_AUTO_TEST_SUITE(ProofOfWork_suite)

BOOST_AUTO_TEST_CASE( ProofOfWork_test )
{
	ProofOfWorkGenerator gen;
	ProofOfWork pow = gen.getProof();
	cLog(lsINFO) << "Estimated difficulty: " << pow.getDifficulty();
	uint256 solution = pow.solve(16777216);
	if (solution.isZero())
		BOOST_FAIL("Unable to solve proof of work");
	if (!pow.checkSolution(solution))
		BOOST_FAIL("Solution did not check");

	cLog(lsDEBUG) << "A bad nonce error is expected";
	POWResult r = gen.checkProof(pow.getToken(), uint256());
	if (r != powBADNONCE)
	{
		Log(lsFATAL) << "POWResult = " << static_cast<int>(r);
		BOOST_FAIL("Empty solution didn't show bad nonce");
	}
	if (gen.checkProof(pow.getToken(), solution) != powOK)
		BOOST_FAIL("Solution did not check with issuer");
	cLog(lsDEBUG) << "A reused nonce error is expected";
	if (gen.checkProof(pow.getToken(), solution) != powREUSED)
		BOOST_FAIL("Reuse solution not detected");

#ifdef SOLVE_POWS
	for (int i = 0; i < 12; ++i)
	{
		gen.setDifficulty(i);
		ProofOfWork pow = gen.getProof();
		cLog(lsINFO) << "Level: " << i << ", Estimated difficulty: " << pow.getDifficulty();
		uint256 solution = pow.solve(131072);
		if (solution.isZero())
			cLog(lsINFO) << "Giving up";
		else
		{
			cLog(lsINFO) << "Solution found";
			if (gen.checkProof(pow.getToken(), solution) != powOK)
			{
				cLog(lsFATAL) << "Solution fails";
			}
		}
	}
#endif

}

BOOST_AUTO_TEST_SUITE_END()

// vim:ts=4
