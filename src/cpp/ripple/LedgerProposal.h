#ifndef __PROPOSELEDGER__
#define __PROPOSELEDGER__

#include <vector>
#include <string>

#include <boost/shared_ptr.hpp>

#include "../json/value.h"

#include "RippleAddress.h"
#include "Serializer.h"
#include "InstanceCounter.h"

DEFINE_INSTANCE(LedgerProposal);

class LedgerProposal : private IS_INSTANCE(LedgerProposal)
{
protected:

	uint256 mPreviousLedger, mCurrentHash, mSuppression;
	uint32 mCloseTime, mProposeSeq;

	uint160			mPeerID;
	RippleAddress	mPublicKey;
	RippleAddress	mPrivateKey;	// If ours

	std::string					mSignature; // set only if needed
	boost::posix_time::ptime	mTime;

public:
	static const uint32 seqLeave = 0xffffffff; // leaving the consensus process

	typedef boost::shared_ptr<LedgerProposal> pointer;

	// proposal from peer
	LedgerProposal(const uint256& prevLgr, uint32 proposeSeq, const uint256& propose,
		uint32 closeTime, const RippleAddress& naPeerPublic, const uint256& suppress);

	// our first proposal
	LedgerProposal(const RippleAddress& pubKey, const RippleAddress& privKey,
		const uint256& prevLedger, const uint256& position,	uint32 closeTime);

	// an unsigned "dummy" proposal for nodes not validating
	LedgerProposal(const uint256& prevLedger, const uint256& position, uint32 closeTime);

	uint256 getSigningHash() const;
	bool checkSign(const std::string& signature, const uint256& signingHash);
	bool checkSign(const std::string& signature) { return checkSign(signature, getSigningHash()); }
	bool checkSign() { return checkSign(mSignature, getSigningHash()); }

	const uint160& getPeerID() const		{ return mPeerID; }
	const uint256& getCurrentHash() const	{ return mCurrentHash; }
	const uint256& getPrevLedger() const	{ return mPreviousLedger; }
	const uint256& getSuppression() const	{ return mSuppression; }
	uint32 getProposeSeq() const			{ return mProposeSeq; }
	uint32 getCloseTime() const				{ return mCloseTime; }
	const RippleAddress& peekPublic() const		{ return mPublicKey; }
	std::vector<unsigned char> getPubKey() const	{ return mPublicKey.getNodePublic(); }
	std::vector<unsigned char> sign();

	void setPrevLedger(const uint256& prevLedger)	{ mPreviousLedger = prevLedger; }
	void setSignature(const std::string& signature)	{ mSignature = signature; }
	bool hasSignature()								{ return !mSignature.empty(); }
	bool isPrevLedger(const uint256& pl)			{ return mPreviousLedger == pl; }
	bool isBowOut()									{ return mProposeSeq == seqLeave; }

	const boost::posix_time::ptime getCreateTime()	{ return mTime; }
	bool isStale(boost::posix_time::ptime cutoff)	{ return mTime <= cutoff; }

	bool changePosition(const uint256& newPosition, uint32 newCloseTime);
	void bowOut();
	Json::Value getJson() const;
};

#endif

// vim:ts=4
