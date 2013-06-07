#ifndef RIPPLE_SERIALIZEDVALIDATION_H
#define RIPPLE_SERIALIZEDVALIDATION_H

DEFINE_INSTANCE (SerializedValidation);

class SerializedValidation
    : public STObject
    , private IS_INSTANCE (SerializedValidation)
{
public:
	typedef boost::shared_ptr<SerializedValidation>			pointer;
	typedef const boost::shared_ptr<SerializedValidation>&	ref;

	static const uint32	sFullFlag = 0x1;

    // These throw if the object is not valid
	SerializedValidation (SerializerIterator& sit, bool checkSignature = true);

	// Does not sign the validation
	SerializedValidation (const uint256& ledgerHash, uint32 signTime, const RippleAddress& raPub, bool isFull);

	uint256			getLedgerHash()		const;
	uint32			getSignTime()		const;
	uint32			getFlags()			const;
	RippleAddress	getSignerPublic()	const;
	uint160			getNodeID()			const	{ return mNodeID; }
	bool			isValid()			const;
	bool			isFull()			const;
	bool			isTrusted()			const	{ return mTrusted; }
	uint256			getSigningHash()	const;
	bool			isValid(const uint256&) const;

	void 						setTrusted()				{ mTrusted = true; }
	std::vector<unsigned char>	getSigned()					const;
	std::vector<unsigned char>	getSignature()				const;
	void sign(uint256& signingHash, const RippleAddress& raPrivate);
	void sign(const RippleAddress& raPrivate);

	// The validation this replaced
	const uint256& getPreviousHash()		{ return mPreviousHash; }
	bool isPreviousHash(const uint256& h) const	{ return mPreviousHash == h; }
	void setPreviousHash(const uint256& h)	{ mPreviousHash = h; }

private:
    static SOTemplate const& getFormat ();

	void setNode ();

    uint256	mPreviousHash;
	uint160 mNodeID;
	bool mTrusted;
};

#endif
// vim:ts=4
