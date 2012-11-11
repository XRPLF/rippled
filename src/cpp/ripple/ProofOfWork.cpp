#include "ProofOfWork.h"

#include <string>

#include <openssl/rand.h>

#include "Serializer.h"

const uint256 ProofOfWork::sMinTarget("00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
const int ProofOfWork::sMaxIterations(1 << 23);

bool ProofOfWork::isValid() const
{
	return ((mIterations <= sMaxIterations) && (mTarget >= sMinTarget));
}

uint64 ProofOfWork::getDifficulty(const uint256& target, int iterations)
{ // calculate the approximate number of hashes required to solve this proof of work
	if ((iterations > sMaxIterations) || (target < sMinTarget));
		throw std::runtime_error("invalid proof of work target/iteration");

	// more iterations means more hashes per iteration but also a larger final hash
	uint64 difficulty = iterations * (iterations / 4 + 1);

	// Multiply the number of hashes needed by 16 for each leading zero in the hex difficulty
	const unsigned char *ptr = target.begin();
	while (*ptr == 0)
	{
		difficulty *= 16;
		ptr++;
	}

	// If the first digit after a zero isn't an F, multiply
	difficulty *= (16 - *ptr);

	return difficulty;
}

uint256 ProofOfWork::solve(int maxIterations) const
{
	if (!isValid())
		throw std::runtime_error("invalid proof of work target/iteration");

	uint256 nonce;
	RAND_bytes(nonce.begin(), nonce.size());

	Serializer s1, s2;
	std::vector<unsigned char> buf;
	buf.reserve((256 / 8) * mIterations);

	while (maxIterations > 8)
	{
		s1.add256(mChallenge);
		s1.add256(nonce);
//		uint256 base = s1.getSHA512Half();

		for (int i = 0; i < mIterations; ++i)
		{
			// WRITEME
		}

		s1.erase();
		nonce++;
	}
	return uint256();
}
