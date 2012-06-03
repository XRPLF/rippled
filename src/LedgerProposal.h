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

	uint256 mPeerID, mPreviousLedger, mCurrentHash;
	uint32 mProposeSeq;
	CKey::pointer mKey;

	static const uint32 sProposeMagic = 0x50525000; // PRP

public:

	typedef boost::shared_ptr<LedgerProposal> pointer;

	// proposal from peer
	LedgerProposal(const uint256& prevLgr, uint32 proposeSeq, const uint256& propose, const std::string& pubKey);

	// our first proposal
	LedgerProposal(CKey::pointer privateKey, const uint256& prevLedger, const uint256& position);

	uint256 getSigningHash() const;
	bool checkSign(const std::string& signature);

	const uint256& getPeerID() const		{ return mPeerID; }
	const uint256& getCurrentHash() const	{ return mCurrentHash; }
	const uint256& getPrevLedger() const	{ return mPreviousLedger; }
	uint32 getProposeSeq() const			{ return mProposeSeq; }
	CKey::pointer peekKey()					{ return mKey; }
	std::vector<unsigned char> getPubKey() const { return mKey->GetPubKey(); }
	std::vector<unsigned char> sign();

	void changePosition(const uint256& newPosition);
};

#endif
