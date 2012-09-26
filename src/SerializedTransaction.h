#ifndef __SERIALIZEDTRANSACTION__
#define __SERIALIZEDTRANSACTION__

#include <vector>

#include <boost/shared_ptr.hpp>

#include "uint256.h"
#include "SerializedObject.h"
#include "TransactionFormats.h"
#include "NewcoinAddress.h"

#define TXN_SQL_NEW			'N'
#define TXN_SQL_CONFLICT	'C'
#define TXN_SQL_HELD		'H'
#define TXN_SQL_VALIDATED	'V'
#define TXN_SQL_INCLUDED	'I'
#define TXN_SQL_UNKNOWN		'U'

class SerializedTransaction : public SerializedType
{
public:
	typedef boost::shared_ptr<SerializedTransaction> pointer;

protected:
	NewcoinAddress mSignPubKey;
	NewcoinAddress mSourceAccount;
	TransactionType mType;
	STVariableLength mSignature;
	STObject mMiddleTxn, mInnerTxn;
	const TransactionFormat* mFormat;

	SerializedTransaction* duplicate() const { return new SerializedTransaction(*this); }

public:
	SerializedTransaction(SerializerIterator& sit);
	SerializedTransaction(TransactionType type);

	// STObject functions
	SerializedTypeID getSType() const { return STI_TRANSACTION; }
	std::string getFullText() const;
	std::string getText() const;
	void add(Serializer& s) const;
	virtual bool isEquivalent(const SerializedType& t) const;

	// outer transaction functions / signature functions
	std::vector<unsigned char> getSignature() const;
	const std::vector<unsigned char>& peekSignature() const;
	void setSignature(const std::vector<unsigned char>& s);
	uint256 getSigningHash() const;

	TransactionType getTxnType() const { return mType; }
	STAmount getTransactionFee() const;
	void setTransactionFee(const STAmount& fee);

	const NewcoinAddress& getSourceAccount() const { return mSourceAccount; }
	std::vector<unsigned char> getSigningPubKey() const;
	const std::vector<unsigned char>& peekSigningPubKey() const;
	std::vector<unsigned char>& peekSigningPubKey();
	const NewcoinAddress& setSigningPubKey(const NewcoinAddress& naSignPubKey);
	const NewcoinAddress& setSourceAccount(const NewcoinAddress& naSource);
	std::string getTransactionType() const { return mFormat->t_name; }

	// inner transaction functions
	uint32 getFlags() const { return mInnerTxn.getFlags(); }
	void setFlag(uint32 v) { mInnerTxn.setFlag(v); }
	void clearFlag(uint32 v) { mInnerTxn.clearFlag(v); }

	uint32 getSequence() const;
	void setSequence(uint32);

	// inner transaction field functions
	int getITFieldIndex(SField::ref field) const;
	int getITFieldCount() const;
	const SerializedType& peekITField(SField::ref field) const;
	SerializedType& getITField(SField::ref field);

	// inner transaction field value functions
	std::string getITFieldString(SField::ref field) const { return mInnerTxn.getFieldString(field); }
	unsigned char getITFieldU8(SField::ref field) const { return mInnerTxn.getValueFieldU8(field); }
	uint16 getITFieldU16(SField::ref field) const { return mInnerTxn.getValueFieldU16(field); }
	uint32 getITFieldU32(SField::ref field) const { return mInnerTxn.getValueFieldU32(field); }
	uint64 getITFieldU64(SField::ref field) const { return mInnerTxn.getValueFieldU64(field); }
	uint128 getITFieldH128(SField::ref field) const { return mInnerTxn.getValueFieldH128(field); }
	uint160 getITFieldH160(SField::ref field) const { return mInnerTxn.getValueFieldH160(field); }
	uint160 getITFieldAccount(SField::ref field) const;
	uint256 getITFieldH256(SField::ref field) const { return mInnerTxn.getValueFieldH256(field); }
	std::vector<unsigned char> getITFieldVL(SField::ref field) const { return mInnerTxn.getValueFieldVL(field); }
	std::vector<TaggedListItem> getITFieldTL(SField::ref field) const { return mInnerTxn.getValueFieldTL(field); }
	STAmount getITFieldAmount(SField::ref field) const { return mInnerTxn.getValueFieldAmount(field); }
	STPathSet getITFieldPathSet(SField::ref field) const { return mInnerTxn.getValueFieldPathSet(field); }

	void setITFieldU8(SField::ref field, unsigned char v) { return mInnerTxn.setValueFieldU8(field, v); }
	void setITFieldU16(SField::ref field, uint16 v) { return mInnerTxn.setValueFieldU16(field, v); }
	void setITFieldU32(SField::ref field, uint32 v) { return mInnerTxn.setValueFieldU32(field, v); }
	void setITFieldU64(SField::ref field, uint32 v) { return mInnerTxn.setValueFieldU64(field, v); }
	void setITFieldH128(SField::ref field, const uint128& v) { return mInnerTxn.setValueFieldH128(field, v); }
	void setITFieldH160(SField::ref field, const uint160& v) { return mInnerTxn.setValueFieldH160(field, v); }
	void setITFieldH256(SField::ref field, const uint256& v) { return mInnerTxn.setValueFieldH256(field, v); }
	void setITFieldVL(SField::ref field, const std::vector<unsigned char>& v)
		{ return mInnerTxn.setValueFieldVL(field, v); }
	void setITFieldTL(SField::ref field, const std::vector<TaggedListItem>& v)
		{ return mInnerTxn.setValueFieldTL(field, v); }
	void setITFieldAccount(SField::ref field, const uint160& v)
		{ return mInnerTxn.setValueFieldAccount(field, v); }
	void setITFieldAccount(SField::ref field, const NewcoinAddress& v)
		{ return mInnerTxn.setValueFieldAccount(field, v); }
	void setITFieldAmount(SField::ref field, const STAmount& v)
		{ return mInnerTxn.setValueFieldAmount(field, v); }
	void setITFieldPathSet(SField::ref field, const STPathSet& v)
		{ return mInnerTxn.setValueFieldPathSet(field, v); }

	// optional field functions
	bool getITFieldPresent(SField::ref field) const;
	void makeITFieldPresent(SField::ref field);
	void makeITFieldAbsent(SField::ref field);

	std::vector<NewcoinAddress> getAffectedAccounts() const;

	uint256 getTransactionID() const;

	virtual Json::Value getJson(int options) const;

	bool sign(const NewcoinAddress& naAccountPrivate);
	bool checkSign(const NewcoinAddress& naAccountPublic) const;

	// SQL Functions
	static std::string getSQLValueHeader();
	static std::string getSQLInsertHeader();
	std::string getSQL(std::string& sql, uint32 inLedger, char status) const;
	std::string getSQL(uint32 inLedger, char status) const;
	std::string getSQL(Serializer rawTxn, uint32 inLedger, char status) const;

};

#endif
// vim:ts=4
