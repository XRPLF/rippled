
#include "LedgerProposal.h"

#include <boost/make_shared.hpp>

#include "key.h"
#include "Application.h"
#include "HashPrefixes.h"

LedgerProposal::LedgerProposal(const uint256& pLgr, uint32 seq, const uint256& tx, uint64 closeTime,
		const NewcoinAddress& naPeerPublic) :
	mPreviousLedger(pLgr), mCurrentHash(tx), mCloseTime(closeTime), mProposeSeq(seq)
{
	mPublicKey		= naPeerPublic;
	// XXX Validate key.
	// if (!mKey->SetPubKey(pubKey))
	// throw std::runtime_error("Invalid public key in proposal");

	mPreviousLedger	= theApp->getMasterLedger().getClosedLedger()->getHash();
	mPeerID			= mPublicKey.getNodeID();
}


LedgerProposal::LedgerProposal(const NewcoinAddress& naSeed, const uint256& prevLgr,
		const uint256& position, uint64 closeTime) :
	mPreviousLedger(prevLgr), mCurrentHash(position), mCloseTime(closeTime), mProposeSeq(0)
{
	mPublicKey	= NewcoinAddress::createNodePublic(naSeed);
	mPrivateKey	= NewcoinAddress::createNodePrivate(naSeed);
	mPeerID		= mPublicKey.getNodeID();
}

LedgerProposal::LedgerProposal(const uint256& prevLgr, const uint256& position, uint64 closeTime) :
	mPreviousLedger(prevLgr), mCurrentHash(position), mCloseTime(closeTime), mProposeSeq(0)
{
	;
}

uint256 LedgerProposal::getSigningHash() const
{
	Serializer s((32 + 32 + 256 + 256 + 64) / 8);

	s.add32(sHP_Proposal);
	s.add32(mProposeSeq);
	s.add256(mPreviousLedger);
	s.add256(mCurrentHash);
	s.add64(mCloseTime);

	return s.getSHA512Half();
}

bool LedgerProposal::checkSign(const std::string& signature, const uint256& signingHash)
{
	return mPublicKey.verifyNodePublic(signingHash, signature);
}

void LedgerProposal::changePosition(const uint256& newPosition, uint64 closeTime)
{
	mCurrentHash = newPosition;
	mCloseTime = closeTime;
	++mProposeSeq;
}

std::vector<unsigned char> LedgerProposal::sign(void)
{
	std::vector<unsigned char> ret;

	mPrivateKey.signNodePrivate(getSigningHash(), ret);
	// XXX If this can fail, find out sooner.
	// if (!mPrivateKey.signNodePrivate(getSigningHash(), ret))
	//	throw std::runtime_error("unable to sign proposal");

	return ret;
}

// vim:ts=4
