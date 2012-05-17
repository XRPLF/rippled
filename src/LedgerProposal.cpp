
#include "LedgerProposal.h"

#include <boost/make_shared.hpp>

LedgerProposal::LedgerProposal(SerializerIterator& it) : mKey(boost::make_shared<CKey>())
{
	if (it.get32() != sProposeMagic)
		throw std::runtime_error("Not a ledger proposal");

	mPreviousLedger = it.get256();
	mCurrentHash = it.get256();
	mPrevHash = it.get256();
	mProposeSeq = it.get32();
	if (mKey->SetPubKey(it.getVL()))
		throw std::runtime_error("Unable to set public key");
	mSignature = it.getVL(); 

	mPeerID = Serializer::getSHA512Half(mKey->GetPubKey());

	if (!mKey->Verify(getSigningHash(), mSignature))
		throw std::runtime_error("Ledger proposal invalid");
}

LedgerProposal::LedgerProposal(CKey::pointer mPrivateKey, const uint256& prevLgr, const uint256& position) :
	mPreviousLedger(prevLgr), mCurrentHash(position), mProposeSeq(0), mKey(mPrivateKey)
{
	mPeerID = Serializer::getSHA512Half(mKey->GetPubKey());
	if (!mKey->Sign(getSigningHash(), mSignature))
		throw std::runtime_error("Unable to sign proposal");
}

LedgerProposal::LedgerProposal(LedgerProposal::pointer previous, const uint256& newp) :
	mPeerID(previous->mPeerID), mPreviousLedger(previous->mPreviousLedger), mPrevHash(previous->mCurrentHash),
	mCurrentHash(newp), mProposeSeq(previous->mProposeSeq + 1), mKey(previous->mKey)
{
	if (!mKey->Sign(getSigningHash(), mSignature))
		throw std::runtime_error("Unable to sign proposal");
}

void LedgerProposal::add(Serializer& s, bool for_signature) const
{
	s.add32(sProposeMagic);
	s.add256(mPreviousLedger);
	s.add256(mCurrentHash);
	s.add256(mPrevHash);
	s.add32(mProposeSeq);

	if (for_signature)
		return;

	s.addVL(mKey->GetPubKey());
	s.addVL(mSignature);
}

uint256 LedgerProposal::getSigningHash() const
{
	Serializer s(104);
	add(s, true);
	return s.getSHA512Half();
}
