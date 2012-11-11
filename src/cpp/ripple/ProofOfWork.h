#ifndef PROOF_OF_WORK__H
#define PROOF_OF_WORK__H

#include <string>

#include <boost/thread/mutex.hpp>
#include <boost/bimap.hpp>
#include <boost/bimap/set_of.hpp>
#include <boost/bimap/multiset_of.hpp>

#include "uint256.h"

class ProofOfWork
{
protected:

	std::string		mToken;
	uint256			mChallenge;
	uint256			mTarget;
	int				mIterations;

	static const uint256 sMinTarget;
	static const int sMaxIterations;

public:
	ProofOfWork(const std::string& token, int iterations, const uint256& challenge, const uint256& target) :
		mToken(token), mChallenge(challenge), mTarget(target), mIterations(iterations)
	{ ; }

	bool isValid() const;

	uint256 solve(int maxIterations) const;
	bool checkSolution(const uint256& solution) const;

	// approximate number of hashes needed to solve
	static uint64 getDifficulty(const uint256& target, int iterations);
	uint64 getDifficulty() const { return getDifficulty(mTarget, mIterations); }
};

class ProofOfWorkGenerator
{
public:
	typedef boost::bimap< boost::bimaps::multiset_of<time_t>, boost::bimaps::set_of<uint256> > powMap_t;
	typedef powMap_t::value_type	powMap_vt;

protected:
	uint256							mSecret;
	int								mIterations;
	uint256							mTarget;
	time_t							mLastDifficultyChange;

	powMap_t						mSolvedChallenges;
	boost::mutex					mLock;

public:
	ProofOfWorkGenerator(const uint256& secret);

	ProofOfWork getProof();
	bool checkProof(const std::string& token, const uint256& solution);

	void loadHigh();
	void loadLow();
	uint64 getDifficulty()	{ return ProofOfWork::getDifficulty(mTarget, mIterations); }
};

#endif
