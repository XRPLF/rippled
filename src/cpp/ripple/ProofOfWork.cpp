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
	return ((mIterations <= sMaxIterations) && (mTarget >= sMinTarget));
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
	uint64 difficulty = iterations + (iterations / 4);

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
		buf1[2] = uint256();
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
		return powBADTOKEN;
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
		if (!mSolvedChallenges.insert(powMap_vt(now, challenge)).second)
		{
			cLog(lsDEBUG) << "PoW " << token << " has been reused";
			return powREUSED;
		}
	}

	return powOK;
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
}

BOOST_AUTO_TEST_SUITE_END()

// vim:ts=4
