#include "ProofOfWork.h"

#include <string>

#include <boost/test/unit_test.hpp>

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
	uint64 difficulty = iterations * (iterations / 4 + 1);

	// Multiply the number of hashes needed by 256 for each leading zero byte in the hex difficulty
	const unsigned char *ptr = target.end() - 1;
	while (*ptr == 0)
	{
		cLog(lsINFO) << "getDif: " << (int) *ptr;
		difficulty *= 256;
		--ptr;
	}

	// If the first digit after a zero isn't an F, multiply
	difficulty *= (256 - *ptr);

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
			if (buf1.size() != 3)
				Log(lsINFO) << "buf1.size=" << buf1.size();
			buf1[2] = getSHA512Half(buf1);
			buf2[i] = buf1[2];
		}
		if (buf2.size() != mIterations)
			Log(lsINFO) << "buf2.size=" << buf2.size();

		uint256 hash = getSHA512Half(buf2);
		if (hash <= mTarget)
			return nonce;

		++nonce;
		--maxIterations;
	}
	return uint256();
}

BOOST_AUTO_TEST_SUITE(ProofOfWork_suite)

BOOST_AUTO_TEST_CASE( ProofOfWork_test )
{
	ProofOfWork pow("test", 32, uint256(),
		uint256("00FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"));
	cLog(lsINFO) << "Estimated difficulty: " << pow.getDifficulty();
	uint256 solution = pow.solve(16777216);
	if (solution.isZero())
		BOOST_FAIL("Unable to solve proof of work");
}

BOOST_AUTO_TEST_SUITE_END()

// vim:ts=4
