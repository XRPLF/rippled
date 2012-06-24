
#include "LedgerProposal.h"

#include <boost/make_shared.hpp>

#include "key.h"
#include "Application.h"

LedgerProposal::LedgerProposal(const uint256& pLgr, uint32 seq, const uint256& tx, const NewcoinAddress& naPeerPublic) :
	mPreviousLedger(pLgr), mCurrentHash(tx), mProposeSeq(seq)
{
	mPublicKey		= naPeerPublic;
	// XXX Validate key.
	// if (!mKey->SetPubKey(pubKey))
	// throw std::runtime_error("Invalid public key in proposal");

	mPreviousLedger	= theApp->getMasterLedger().getClosedLedger()->getHash();
	mPeerID			= mPublicKey.getNodeID();
}


LedgerProposal::LedgerProposal(const NewcoinAddress& naSeed, const uint256& prevLgr, const uint256& position) :
	mPreviousLedger(prevLgr), mCurrentHash(position), mProposeSeq(0)
{
	mPublicKey	= NewcoinAddress::createNodePublic(naSeed);
	mPrivateKey	= NewcoinAddress::createNodePrivate(naSeed);
	mPeerID		= mPublicKey.getNodeID();
}

LedgerProposal::LedgerProposal(const uint256& prevLgr, const uint256& position) :
	mPreviousLedger(prevLgr), mCurrentHash(position), mProposeSeq(0)
{
	;
}

uint256 LedgerProposal::getSigningHash() const
{
	Serializer s(72);

	s.add32(sProposeMagic);
	s.add32(mProposeSeq);
	s.add256(mPreviousLedger);
	s.add256(mCurrentHash);

	return s.getSHA512Half();
}

bool LedgerProposal::checkSign(const std::string& signature, const uint256& signingHash)
{
	return mPublicKey.verifyNodePublic(signingHash, signature);
}

void LedgerProposal::changePosition(const uint256& newPosition)
{
	mCurrentHash = newPosition;
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
