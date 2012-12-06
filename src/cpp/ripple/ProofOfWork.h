#ifndef PROOF_OF_WORK__H
#define PROOF_OF_WORK__H

#include <string>

#include <boost/thread/mutex.hpp>
#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/bimap/multiset_of.hpp>

#include "uint256.h"

enum POWResult
{
	powOK		= 0,
	powREUSED	= 1, // already submitted
	powBADNONCE	= 2, // you didn't solve it
	powEXPIRED	= 3, // time is up
	powCORRUPT	= 4,
	powTOOEASY	= 5, // the difficulty increased too much while you solved it
};

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
	typedef boost::shared_ptr<ProofOfWork> pointer;

	ProofOfWork(const std::string& token, int iterations, const uint256& challenge, const uint256& target) :
		mToken(token), mChallenge(challenge), mTarget(target), mIterations(iterations)
	{ ; }

	bool isValid() const;

	uint256 solve(int maxIterations = 2 * sMaxIterations) const;
	bool checkSolution(const uint256& solution) const;

	const std::string& getToken() const		{ return mToken; }
	const uint256& getChallenge() const		{ return mChallenge; }

	// approximate number of hashes needed to solve
	static uint64 getDifficulty(const uint256& target, int iterations);
	uint64 getDifficulty() const { return getDifficulty(mTarget, mIterations); }
};

class ProofOfWorkGenerator
{
public:
	typedef boost::bimap< boost::bimaps::multiset_of<time_t>, boost::bimaps::unordered_set_of<uint256> > powMap_t;
	typedef powMap_t::value_type	powMap_vt;

protected:
	uint256							mSecret;
	int								mIterations;
	uint256							mTarget;
	time_t							mLastDifficultyChange;
	int								mValidTime;
	int								mPowEntry;

	powMap_t						mSolvedChallenges;
	boost::mutex					mLock;

public:
	ProofOfWorkGenerator();

	ProofOfWork getProof();
	POWResult checkProof(const std::string& token, const uint256& solution);
	uint64 getDifficulty()	{ return ProofOfWork::getDifficulty(mTarget, mIterations); }
	void setDifficulty(int i);

	void loadHigh();
	void loadLow();
	void sweep(void);

	static int getPowEntry(const uint256& target, int iterations);
};

#endif

// vim:ts=4
