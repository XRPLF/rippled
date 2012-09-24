#ifndef __SERIALIZEDOBJECT__
#define __SERIALIZEDOBJECT__

#include <vector>

#include <boost/ptr_container/ptr_vector.hpp>

#include "../json/value.h"

#include "SerializedTypes.h"

enum SOE_Type
{
	SOE_NEVER = -1,		// never occurs (marks end of object)
	SOE_REQUIRED = 0,	// required
	SOE_FLAGS = 1,		// flags field
	SOE_IFFLAG = 2,		// present if flag set
	SOE_IFNFLAG = 3		// present if flag not set
};

// JED: seems like there would be a better way to do this
// maybe something that inherits from SerializedTransaction
enum SOE_Field
{
	sfInvalid = -1,
	sfGeneric = 0,

	// common fields
	sfAcceptExpire,
	sfAcceptRate,
	sfAcceptStart,
	sfAccount,
	sfAccountHash,
	sfAmount,
	sfAuthorizedKey,
	sfBalance,
	sfBaseFee,
	sfBondAmount,
	sfBookDirectory,
	sfBookNode,
	sfBorrowExpire,
	sfBorrowRate,
	sfBorrowStart,
	sfBorrower,
	sfCreateCode,
	sfCloseTime,
	sfCloseResolution,
	sfCurrency,
	sfCurrencyIn,
	sfCurrencyOut,
	sfDestination,
	sfDomain,
	sfEmailHash,
	sfExpiration,
	sfExpireCode,
	sfExtensions,
	sfFirstNode,
	sfFlags,
	sfFundCode,
	sfGenerator,
	sfGeneratorID,
	sfHash,
	sfHighID,
	sfHighLimit,
	sfHighQualityIn,
	sfHighQualityOut,
	sfIdentifier,
	sfIndexes,
	sfIndexNext,
	sfIndexPrevious,
	sfInnerTransaction,
	sfInvoiceID,
	sfIssuer,
	sfLastNode,
	sfLastTxnID,
	sfLastTxnSeq,
	sfLedgerHash,
	sfLedgerSequence,
	sfLimitAmount,
	sfLoadFee,
	sfLowID,
	sfLowLimit,
	sfLowQualityIn,
	sfLowQualityOut,
	sfMessageKey,
	sfMiddleTransaction,
	sfMinimumOffer,
	sfNextAcceptExpire,
	sfNextAcceptRate,
	sfNextAcceptStart,
	sfNextTransitExpire,
	sfNextTransitRate,
	sfNextTransitStart,
	sfNickname,
	sfOfferSequence,
	sfOwner,
	sfOwnerNode,
	sfParentCloseTime,
	sfParentHash,
	sfPaths,
	sfPublicKey,
	sfPublishHash,
	sfPublishSize,
	sfQualityIn,
	sfQualityOut,
	sfRemoveCode,
	sfRippleEscrow,
	sfSendMax,
	sfSequence,
	sfSignature,
	sfSigningAccounts,
	sfSigningKey,
	sfSigningTime,
	sfSourceTag,
	sfStampEscrow,
	sfTakerGets,
	sfTakerPays,
	sfTarget,
	sfTransferRate,
	sfTransactionHash,
	sfVersion,
	sfWalletLocator,

	// test fields
	sfTest1, sfTest2, sfTest3, sfTest4
};

struct SOElement
{ // An element in the description of a serialized object
	SOE_Field e_field;
	const char *e_name;
	SerializedTypeID e_id;
	SOE_Type e_type;
	int e_flags;
};

class STObject : public SerializedType
{
protected:
	int mFlagIdx; // the offset to the flags object, -1 if none
	boost::ptr_vector<SerializedType> mData;
	std::vector<const SOElement*> mType;

	STObject* duplicate() const { return new STObject(*this); }

public:
	STObject(const char *n = NULL) : SerializedType(n), mFlagIdx(-1) { ; }
	STObject(const SOElement *t, const char *n = NULL);
	STObject(const SOElement *t, SerializerIterator& u, const char *n = NULL);
	virtual ~STObject() { ; }

	void set(const SOElement* t);
	void set(const SOElement* t, SerializerIterator& u);

	int getLength() const;
	virtual SerializedTypeID getSType() const { return STI_OBJECT; }
	virtual bool isEquivalent(const SerializedType& t) const;

	void add(Serializer& s) const;
	Serializer getSerializer() const { Serializer s; add(s); return s; }
	std::string getFullText() const;
	std::string getText() const;
	virtual Json::Value getJson(int options) const;

	int addObject(const SerializedType& t) { mData.push_back(t.clone()); return mData.size() - 1; }
	int giveObject(std::auto_ptr<SerializedType> t) { mData.push_back(t); return mData.size() - 1; }
	int giveObject(SerializedType* t) { mData.push_back(t); return mData.size() - 1; }
	const boost::ptr_vector<SerializedType>& peekData() const { return mData; }
	boost::ptr_vector<SerializedType>& peekData() { return mData; }

	int getCount() const { return mData.size(); }

	bool setFlag(uint32);
	bool clearFlag(uint32);
	uint32 getFlags() const;

	const SerializedType& peekAtIndex(int offset) const { return mData[offset]; }
	SerializedType& getIndex(int offset) { return mData[offset]; }
	const SerializedType* peekAtPIndex(int offset) const { return &(mData[offset]); }
	SerializedType* getPIndex(int offset) { return &(mData[offset]); }

	int getFieldIndex(SOE_Field field) const;
	SOE_Field getFieldSType(int index) const;

	const SerializedType& peekAtField(SOE_Field field) const;
	SerializedType& getField(SOE_Field field);
	const SerializedType* peekAtPField(SOE_Field field) const;
	SerializedType* getPField(SOE_Field field);
	const SOElement* getFieldType(SOE_Field field) const;

	// these throw if the field type doesn't match, or return default values if the
	// field is optional but not present
	std::string getFieldString(SOE_Field field) const;
	unsigned char getValueFieldU8(SOE_Field field) const;
	uint16 getValueFieldU16(SOE_Field field) const;
	uint32 getValueFieldU32(SOE_Field field) const;
	uint64 getValueFieldU64(SOE_Field field) const;
	uint128 getValueFieldH128(SOE_Field field) const;
	uint160 getValueFieldH160(SOE_Field field) const;
	uint256 getValueFieldH256(SOE_Field field) const;
	NewcoinAddress getValueFieldAccount(SOE_Field field) const;
	std::vector<unsigned char> getValueFieldVL(SOE_Field field) const;
	std::vector<TaggedListItem> getValueFieldTL(SOE_Field field) const;
	STAmount getValueFieldAmount(SOE_Field field) const;
	STPathSet getValueFieldPathSet(SOE_Field field) const;
	STVector256 getValueFieldV256(SOE_Field field) const;

	void setValueFieldU8(SOE_Field field, unsigned char);
	void setValueFieldU16(SOE_Field field, uint16);
	void setValueFieldU32(SOE_Field field, uint32);
	void setValueFieldU64(SOE_Field field, uint64);
	void setValueFieldH128(SOE_Field field, const uint128&);
	void setValueFieldH160(SOE_Field field, const uint160&);
	void setValueFieldH256(SOE_Field field, const uint256&);
	void setValueFieldVL(SOE_Field field, const std::vector<unsigned char>&);
	void setValueFieldTL(SOE_Field field, const std::vector<TaggedListItem>&);
	void setValueFieldAccount(SOE_Field field, const uint160&);
	void setValueFieldAccount(SOE_Field field, const NewcoinAddress& addr)
	{ setValueFieldAccount(field, addr.getAccountID()); }
	void setValueFieldAmount(SOE_Field field, const STAmount&);
	void setValueFieldPathSet(SOE_Field field, const STPathSet&);
	void setValueFieldV256(SOE_Field field, const STVector256& v);

	bool isFieldPresent(SOE_Field field) const;
	SerializedType* makeFieldPresent(SOE_Field field);
	void makeFieldAbsent(SOE_Field field);

	static std::auto_ptr<SerializedType> makeDefaultObject(SerializedTypeID id, const char *name);
	static std::auto_ptr<SerializedType> makeDeserializedObject(SerializedTypeID id, const char *name,
		SerializerIterator&);

	static void unitTest();
};


#endif
// vim:ts=4
