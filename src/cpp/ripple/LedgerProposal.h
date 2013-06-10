#ifndef __PROPOSELEDGER__
#define __PROPOSELEDGER__

DEFINE_INSTANCE(LedgerProposal);

class LedgerProposal : private IS_INSTANCE(LedgerProposal)
{
public:
	static const uint32 seqLeave = 0xffffffff; // leaving the consensus process

	typedef boost::shared_ptr<LedgerProposal> pointer;
	typedef const pointer& ref;

	// proposal from peer
	LedgerProposal(uint256 const& prevLgr, uint32 proposeSeq, uint256 const& propose,
		uint32 closeTime, const RippleAddress& naPeerPublic, uint256 const& suppress);

	// our first proposal
	LedgerProposal(const RippleAddress& pubKey, const RippleAddress& privKey,
		uint256 const& prevLedger, uint256 const& position,	uint32 closeTime);

	// an unsigned "dummy" proposal for nodes not validating
	LedgerProposal(uint256 const& prevLedger, uint256 const& position, uint32 closeTime);

	uint256 getSigningHash() const;
	bool checkSign(const std::string& signature, uint256 const& signingHash);
	bool checkSign(const std::string& signature) { return checkSign(signature, getSigningHash()); }
	bool checkSign() { return checkSign(mSignature, getSigningHash()); }

	const uint160& getPeerID() const		{ return mPeerID; }
	uint256 const& getCurrentHash() const	{ return mCurrentHash; }
	uint256 const& getPrevLedger() const	{ return mPreviousLedger; }
	uint256 const& getHashRouter() const	{ return mSuppression; }
	uint32 getProposeSeq() const			{ return mProposeSeq; }
	uint32 getCloseTime() const				{ return mCloseTime; }
	const RippleAddress& peekPublic() const		{ return mPublicKey; }
	Blob getPubKey() const	{ return mPublicKey.getNodePublic(); }
	Blob sign();

	void setPrevLedger(uint256 const& prevLedger)	{ mPreviousLedger = prevLedger; }
	void setSignature(const std::string& signature)	{ mSignature = signature; }
	bool hasSignature()								{ return !mSignature.empty(); }
	bool isPrevLedger(uint256 const& pl)			{ return mPreviousLedger == pl; }
	bool isBowOut()									{ return mProposeSeq == seqLeave; }

	const boost::posix_time::ptime getCreateTime()	{ return mTime; }
	bool isStale(boost::posix_time::ptime cutoff)	{ return mTime <= cutoff; }

	bool changePosition(uint256 const& newPosition, uint32 newCloseTime);
	void bowOut();
	Json::Value getJson() const;

private:
	uint256 mPreviousLedger, mCurrentHash, mSuppression;
	uint32 mCloseTime, mProposeSeq;

	uint160			mPeerID;
	RippleAddress	mPublicKey;
	RippleAddress	mPrivateKey;	// If ours

	std::string					mSignature; // set only if needed
	boost::posix_time::ptime	mTime;
};

#endif

// vim:ts=4
