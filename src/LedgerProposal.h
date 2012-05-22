#ifndef __PROPOSELEDGER__
#define __PROPOSELEDEGR__

#include <vector>

#include <boost/shared_ptr.hpp>

#include "uint256.h"
#include "key.h"
#include "Serializer.h"

class LedgerProposal
{
protected:

	uint256 mPeerID, mPreviousLedger, mPrevHash, mCurrentHash;
	uint32 mProposeSeq;
	CKey::pointer mKey;
//	std::vector<uint256> mAddedTx, mRemovedTx;

	static const uint32 sProposeMagic = 0x50525000; // PRP

public:

	typedef boost::shared_ptr<LedgerProposal> pointer;

	// proposal from peer
	LedgerProposal(uint32 closingSeq, uint32 proposeSeq, const uint256& prevTx, const uint256& propose,
		const std::string& pubKey);

	// our first proposal
	LedgerProposal(CKey::pointer privateKey, const uint256& prevLedger, const uint256& position);

	// our following proposals
	LedgerProposal(LedgerProposal::pointer previous, const uint256& newPosition);

	uint256 getSigningHash() const;
	bool checkSign(const std::string& signature);

	const uint256& getPeerID() const		{ return mPeerID; }
	const uint256& getPrevHash() const		{ return mPrevHash; }
	const uint256& getCurrentHash() const	{ return mCurrentHash; }
	const uint256& getPrevLedger() const	{ return mPreviousLedger; }
	uint32 getProposeSeq() const			{ return mProposeSeq; }

};

#endif
