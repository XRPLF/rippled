
DECLARE_INSTANCE(LedgerProposal);

LedgerProposal::LedgerProposal(uint256 const& pLgr, uint32 seq, uint256 const& tx, uint32 closeTime,
	const RippleAddress& naPeerPublic, uint256 const& suppression) :
		mPreviousLedger(pLgr), mCurrentHash(tx), mSuppression(suppression), mCloseTime(closeTime),
		mProposeSeq(seq), mPublicKey(naPeerPublic)
{
	// XXX Validate key.
	// if (!mKey->SetPubKey(pubKey))
	// throw std::runtime_error("Invalid public key in proposal");

	mPeerID			= mPublicKey.getNodeID();
	mTime			= boost::posix_time::second_clock::universal_time();
}


LedgerProposal::LedgerProposal(const RippleAddress& naPub, const RippleAddress& naPriv,
		uint256 const& prevLgr,	uint256 const& position, uint32 closeTime) :
	mPreviousLedger(prevLgr), mCurrentHash(position), mCloseTime(closeTime), mProposeSeq(0),
	mPublicKey(naPub), mPrivateKey(naPriv)
{
	mPeerID		= mPublicKey.getNodeID();
	mTime		= boost::posix_time::second_clock::universal_time();
}

LedgerProposal::LedgerProposal(uint256 const& prevLgr, uint256 const& position, uint32 closeTime) :
	mPreviousLedger(prevLgr), mCurrentHash(position), mCloseTime(closeTime), mProposeSeq(0)
{
	mTime		= boost::posix_time::second_clock::universal_time();
}

uint256 LedgerProposal::getSigningHash() const
{
	Serializer s((32 + 32 + 32 + 256 + 256) / 8);

	s.add32(theConfig.SIGN_PROPOSAL);
	s.add32(mProposeSeq);
	s.add32(mCloseTime);
	s.add256(mPreviousLedger);
	s.add256(mCurrentHash);

	return s.getSHA512Half();
}

bool LedgerProposal::checkSign(const std::string& signature, uint256 const& signingHash)
{
	return mPublicKey.verifyNodePublic(signingHash, signature);
}

bool LedgerProposal::changePosition(uint256 const& newPosition, uint32 closeTime)
{
	if (mProposeSeq == seqLeave)
		return false;

	mCurrentHash 	= newPosition;
	mCloseTime		= closeTime;
	mTime			= boost::posix_time::second_clock::universal_time();
	++mProposeSeq;
	return true;
}

void LedgerProposal::bowOut()
{
	mTime			= boost::posix_time::second_clock::universal_time();
	mProposeSeq		= seqLeave;
}

Blob LedgerProposal::sign(void)
{
	Blob ret;

	mPrivateKey.signNodePrivate(getSigningHash(), ret);
	// XXX If this can fail, find out sooner.
	// if (!mPrivateKey.signNodePrivate(getSigningHash(), ret))
	//	throw std::runtime_error("unable to sign proposal");

	return ret;
}

Json::Value LedgerProposal::getJson() const
{
	Json::Value ret = Json::objectValue;
	ret["previous_ledger"] = mPreviousLedger.GetHex();

	if (mProposeSeq != seqLeave)
	{
		ret["transaction_hash"] = mCurrentHash.GetHex();
		ret["propose_seq"] = mProposeSeq;
	}

	ret["close_time"] = mCloseTime;

	if (mPublicKey.isValid())
		ret["peer_id"] = mPublicKey.humanNodePublic();

	return ret;
}

// vim:ts=4
