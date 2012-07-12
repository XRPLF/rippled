#ifndef __PROPOSELEDGER__
#define __PROPOSELEDEGR__

#include <vector>

#include <boost/shared_ptr.hpp>

#include "NewcoinAddress.h"
#include "Serializer.h"

class LedgerProposal
{
protected:

	uint256 mPreviousLedger, mCurrentHash;
	uint32 mCloseTime, mProposeSeq;

	uint160			mPeerID;
	NewcoinAddress	mPublicKey;
	NewcoinAddress	mPrivateKey;	// If ours

public:

	typedef boost::shared_ptr<LedgerProposal> pointer;

	// proposal from peer
	LedgerProposal(const uint256& prevLgr, uint32 proposeSeq, const uint256& propose,
		uint32 closeTime, const NewcoinAddress& naPeerPublic);

	// our first proposal
	LedgerProposal(const NewcoinAddress& privKey, const uint256& prevLedger, const uint256& position,
		uint32 closeTime);

	// an unsigned "dummy" proposal for nodes not validating
	LedgerProposal(const uint256& prevLedger, const uint256& position, uint32 closeTime);

	uint256 getSigningHash() const;
	bool checkSign(const std::string& signature, const uint256& signingHash);
	bool checkSign(const std::string& signature) { return checkSign(signature, getSigningHash()); }

	const uint160& getPeerID() const		{ return mPeerID; }
	const uint256& getCurrentHash() const	{ return mCurrentHash; }
	const uint256& getPrevLedger() const	{ return mPreviousLedger; }
	uint32 getProposeSeq() const			{ return mProposeSeq; }
	uint32 getCloseTime() const				{ return mCloseTime; }
	const NewcoinAddress& peekPublic() const	{ return mPublicKey; }
	std::vector<unsigned char> getPubKey() const { return mPublicKey.getNodePublic(); }
	std::vector<unsigned char> sign();

	void changePosition(const uint256& newPosition, uint32 newCloseTime);
};

#endif

// vim:ts=4
