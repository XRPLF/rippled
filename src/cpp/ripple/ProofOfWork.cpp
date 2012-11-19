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
	RAND_bytes(nonce.begin(), nonce.size());

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

ProofOfWorkGenerator::ProofOfWorkGenerator() :
	mIterations(128),
	mTarget("0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"),
	mLastDifficultyChange(time(NULL)),
	mValidTime(180)
{
	RAND_bytes(mSecret.begin(), mSecret.size());
}

ProofOfWork ProofOfWorkGenerator::getProof()
{
	// challenge - target - iterations - time - validator
	static boost::format f("%s-%s-%d-%d");

	int now = static_cast<int>(time(NULL) / 4);

	uint256 challenge;
	RAND_bytes(challenge.begin(), challenge.size());

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

	{
		boost::mutex::scoped_lock sl(mLock);
		if ((t * 4) > (now + mValidTime))
		{
			cLog(lsDEBUG) << "PoW " << token << " has expired";
			return powEXPIRED;
		}
	}


	ProofOfWork pow(token, lexical_cast_s<int>(fields[2]), challenge, target);
	if (!pow.checkSolution(solution))
	{
		cLog(lsDEBUG) << "PoW " << token << " has a bad nonce";
		return powBADNONCE;
	}

	{
		boost::mutex::scoped_lock sl(mLock);
//		if (...) return powTOOEASY;
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

struct PowEntry
{
	const char *target;
	int iterations;
};

PowEntry PowEntries[32] =
{
	// FIXME: These targets are too low and iteration counts too low
	// These get too difficulty before they become sufficently RAM intensive
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 256 }, // 	Hashes:5242880		KB=8
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 512 }, // 	Hashes:5242880		KB=16
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 512 }, // 	Hashes:10485760		KB=16
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 1024 }, // 	Hashes:10485760		KB=32
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 1024 }, // 	Hashes:20971520		KB=32
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 2048 }, // 	Hashes:20971520		KB=64
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 2048 }, // 	Hashes:41943040		KB=64
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 4096 }, // 	Hashes:41943040		KB=128
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 4096 }, // 	Hashes:83886080		KB=128
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 8192 }, // 	Hashes:83886080		KB=256
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 8192 }, // 	Hashes:167772160	KB=256
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 16384 }, // 	Hashes:167772160	KB=512
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 32768 }, // 	Hashes:335544320	MB=1
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 32768 }, // 	Hashes:671088640	MB=1
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 65536 }, // 	Hashes:671088640	MB=2
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 65536 }, // 	Hashes:1342177280	MB=2
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 131072 }, // 	Hashes:1342177280	MB=4
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 131072 }, // 	Hashes:2684354560	MB=4
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 262144 }, // 	Hashes:2684354560	MB=8
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 524288 }, // 	Hashes:5368709120	MB=16
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 524288 }, // 	Hashes:10737418240	MB=16
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 1048576 }, // Hashes:10737418240	MB=32
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 1048576 }, // Hashes:21474836480	MB=32
	{ "0007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 2097152 }, // Hashes:21474836480	MB=64
	{ "0003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 2097152 }, // Hashes:42949672960	MB=64
	{ "00007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 524288 }, // 	Hashes:85899345920	MB=16
	{ "00003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 524288 }, // 	Hashes:171798691840	MB=16
	{ "00007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 1048576 }, // Hashes:171798691840	MB=32
	{ "00003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 1048576 }, // Hashes:343597383680	MB=32
	{ "00007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 2097152 }, // Hashes:343597383680	MB=64
	{ "00003FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 2097152 }, // Hashes:687194767360	MB=64
};

void ProofOfWorkGenerator::setDifficulty(int i)
{
	assert((i >= 0) && (i <= 31));
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
	if (gen.checkProof(pow.getToken(), uint256()) != powBADNONCE)
		BOOST_FAIL("Empty solution didn't show bad nonce");
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
