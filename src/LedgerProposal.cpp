
#include "LedgerProposal.h"

#include <boost/make_shared.hpp>

#include "key.h"
#include "Application.h"

LedgerProposal::LedgerProposal(uint32 closingSeq, uint32 proposeSeq, const uint256& proposeTx,
	const std::string& pubKey) : mCurrentHash(proposeTx),
	mProposeSeq(proposeSeq), mKey(boost::make_shared<CKey>())
{
	if (!mKey->SetPubKey(pubKey))
		throw std::runtime_error("Invalid public key in proposal");
	mPreviousLedger = theApp->getMasterLedger().getClosedLedger()->getHash();
	mPeerID = Serializer::getSHA512Half(mKey->GetPubKey());
}


LedgerProposal::LedgerProposal(CKey::pointer mPrivateKey, const uint256& prevLgr, const uint256& position) :
	mPreviousLedger(prevLgr), mCurrentHash(position), mProposeSeq(0), mKey(mPrivateKey)
{
	mPeerID = Serializer::getSHA512Half(mKey->GetPubKey());
}

LedgerProposal::LedgerProposal(LedgerProposal::pointer previous, const uint256& newp) :
	mPeerID(previous->mPeerID),	mPreviousLedger(previous->mPreviousLedger),
	mCurrentHash(newp),	mProposeSeq(previous->mProposeSeq + 1), mKey(previous->mKey)
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

bool LedgerProposal::checkSign(const std::string& signature)
{
	return mKey->Verify(getSigningHash(), signature);
}
