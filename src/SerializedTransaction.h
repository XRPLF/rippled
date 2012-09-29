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

class SerializedTransaction : public STObject
{
public:
	typedef boost::shared_ptr<SerializedTransaction> pointer;

protected:
	TransactionType mType;
	const TransactionFormat* mFormat;

	SerializedTransaction* duplicate() const { return new SerializedTransaction(*this); }

public:
	SerializedTransaction(SerializerIterator& sit);
	SerializedTransaction(TransactionType type);

	// STObject functions
	SerializedTypeID getSType() const { return STI_TRANSACTION; }
	std::string getFullText() const;
	std::string getText() const;

	// outer transaction functions / signature functions
	std::vector<unsigned char> getSignature() const;
	void setSignature(const std::vector<unsigned char>& s)	{ setValueFieldVL(sfSignature, s); }
	uint256 getSigningHash() const;

	TransactionType getTxnType() const			{ return mType; }
	STAmount getTransactionFee() const			{ return getValueFieldAmount(sfFee); }
	void setTransactionFee(const STAmount& fee)	{ setValueFieldAmount(sfFee, fee); }

	NewcoinAddress getSourceAccount() const		{ return getValueFieldAccount(sfAccount); }
	std::vector<unsigned char> getSigningPubKey() const { return getValueFieldVL(sfSigningPubKey); }
	void setSigningPubKey(const NewcoinAddress& naSignPubKey);
	void setSourceAccount(const NewcoinAddress& naSource);
	std::string getTransactionType() const { return mFormat->t_name; }

	uint32 getSequence() const		{ return getValueFieldU32(sfSequence); }
	void setSequence(uint32 seq)	{ return setValueFieldU32(sfSequence, seq); }

	// inner transaction field functions (OBSOLETE - use STObject functions)
	int getITFieldIndex(SField::ref field) const				{ return getFieldIndex(field); }
	const SerializedType& peekITField(SField::ref field) const	{ return peekAtField(field); } 
	SerializedType& getITField(SField::ref field)				{ return getField(field); }

	// inner transaction field value functions (OBSOLETE - use STObject functions)
	std::string getITFieldString(SField::ref field) const { return getFieldString(field); }
	unsigned char getITFieldU8(SField::ref field) const { return getValueFieldU8(field); }
	uint16 getITFieldU16(SField::ref field) const { return getValueFieldU16(field); }
	uint32 getITFieldU32(SField::ref field) const { return getValueFieldU32(field); }
	uint64 getITFieldU64(SField::ref field) const { return getValueFieldU64(field); }
	uint128 getITFieldH128(SField::ref field) const { return getValueFieldH128(field); }
	uint160 getITFieldH160(SField::ref field) const { return getValueFieldH160(field); }
	uint160 getITFieldAccount(SField::ref field) const;
	uint256 getITFieldH256(SField::ref field) const { return getValueFieldH256(field); }
	std::vector<unsigned char> getITFieldVL(SField::ref field) const { return getValueFieldVL(field); }
	std::vector<TaggedListItem> getITFieldTL(SField::ref field) const { return getValueFieldTL(field); }
	STAmount getITFieldAmount(SField::ref field) const { return getValueFieldAmount(field); }
	STPathSet getITFieldPathSet(SField::ref field) const { return getValueFieldPathSet(field); }

	void setITFieldU8(SField::ref field, unsigned char v) { return setValueFieldU8(field, v); }
	void setITFieldU16(SField::ref field, uint16 v) { return setValueFieldU16(field, v); }
	void setITFieldU32(SField::ref field, uint32 v) { return setValueFieldU32(field, v); }
	void setITFieldU64(SField::ref field, uint32 v) { return setValueFieldU64(field, v); }
	void setITFieldH128(SField::ref field, const uint128& v) { return setValueFieldH128(field, v); }
	void setITFieldH160(SField::ref field, const uint160& v) { return setValueFieldH160(field, v); }
	void setITFieldH256(SField::ref field, const uint256& v) { return setValueFieldH256(field, v); }
	void setITFieldVL(SField::ref field, const std::vector<unsigned char>& v)
		{ return setValueFieldVL(field, v); }
	void setITFieldTL(SField::ref field, const std::vector<TaggedListItem>& v)
		{ return setValueFieldTL(field, v); }
	void setITFieldAccount(SField::ref field, const uint160& v)
		{ return setValueFieldAccount(field, v); }
	void setITFieldAccount(SField::ref field, const NewcoinAddress& v)
		{ return setValueFieldAccount(field, v); }
	void setITFieldAmount(SField::ref field, const STAmount& v)
		{ return setValueFieldAmount(field, v); }
	void setITFieldPathSet(SField::ref field, const STPathSet& v)
		{ return setValueFieldPathSet(field, v); }

	// optional field functions (OBSOLETE - use STObject functions)
	bool getITFieldPresent(SField::ref field) const	{ return isFieldPresent(field); }
	void makeITFieldPresent(SField::ref field)		{ makeFieldPresent(field); }
	void makeITFieldAbsent(SField::ref field)		{ makeFieldAbsent(field); }

	std::vector<NewcoinAddress> getAffectedAccounts() const;

	uint256 getTransactionID() const;

	virtual Json::Value getJson(int options) const;

	void sign(const NewcoinAddress& naAccountPrivate);
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
