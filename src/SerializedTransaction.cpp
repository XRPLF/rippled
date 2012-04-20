
#include "SerializedTransaction.h"

SerializedTransaction::SerializedTransaction(TransactionType type) : mType(type)
{
	mFormat = getTxnFormat(type);
	if (mFormat == NULL) throw std::runtime_error("invalid transaction type");

	mMiddleTxn.giveObject(new STUInt32("Magic", TransactionMagic));
	mMiddleTxn.giveObject(new STVariableLength("SigningAccount"));
	mMiddleTxn.giveObject(new STUInt32("Sequence"));
	mMiddleTxn.giveObject(new STUInt8("Type", static_cast<unsigned char>(type)));
	mMiddleTxn.giveObject(new STUInt64("Fee"));

	mInnerTxn=STObject(mFormat->elements, "InnerTransaction");
}

SerializedTransaction::SerializedTransaction(SerializerIterator& sit, int length)
{
	if (length == -1) length=sit.getBytesLeft();
	else if (length == 0) length=sit.get32();
	if ( (length < TransactionMinLen) || (length > TransactionMaxLen) )
		throw std::runtime_error("Transaction length invalid");

	mSignature.setValue(sit.getVL());

	if (sit.get32() != TransactionMagic)
		throw std::runtime_error("Transaction has invalid magic");

	mMiddleTxn.giveObject(new STUInt32("Magic", TransactionMagic));
	mMiddleTxn.giveObject(new STVariableLength("SigningAccount", sit.getVL()));
	mMiddleTxn.giveObject(new STUInt32("Sequence", sit.get32()));

	mType = static_cast<TransactionType>(sit.get32());
	mMiddleTxn.giveObject(new STUInt32("Type", static_cast<uint32>(mType)));
	mFormat = getTxnFormat(mType);
	if (!mFormat) throw std::runtime_error("Transaction has invalid type");
	mMiddleTxn.giveObject(new STUInt64("Fee", sit.get64()));

	mInnerTxn = STObject(mFormat->elements, sit, "InnerTransaction");
	updateSigningAccount();
}

void SerializedTransaction::updateSigningAccount()
{
	NewcoinAddress a;
	a.setAccountPublic(peekRawSigningAccount());
	mSigningAccount = a.getAccountID();
}

int SerializedTransaction::getLength() const
{
	return mSignature.getLength() + mMiddleTxn.getLength() + mInnerTxn.getLength();
}

std::string SerializedTransaction::getFullText() const
{
	std::string ret = "\"";
	ret += getTransactionID().GetHex();
	ret += "\" = {";
	ret += mSignature.getFullText();
	ret += mMiddleTxn.getFullText();
	ret += mInnerTxn.getFullText();
	ret += "}";
	return ret;
}

std::string SerializedTransaction::getText() const
{
	std::string ret = "{";
	ret += mSignature.getText();
	ret += mMiddleTxn.getText();
	ret += mInnerTxn.getText();
	ret += "}";
	return ret;
}

int SerializedTransaction::getTransaction(Serializer& s, bool include_length) const
{
	int l = getLength();
	if (include_length) s.add32(l);
	mSignature.add(s);
	mMiddleTxn.add(s);
	mInnerTxn.add(s);
	return l;
}

bool SerializedTransaction::isEquivalent(const SerializedType& t) const
{ // Signatures are not compared
	const SerializedTransaction* v = dynamic_cast<const SerializedTransaction*>(&t);
	if (!v) return false;
	if (mType != v->mType) return false;
	if (mMiddleTxn != v->mMiddleTxn) return false;
	if (mInnerTxn != v->mInnerTxn) return false;
	return true;
}

uint256 SerializedTransaction::getSigningHash() const
{
	Serializer s;
	mMiddleTxn.add(s);
	mInnerTxn.add(s);
	return s.getSHA512Half();
}

uint256 SerializedTransaction::getTransactionID() const
{ // perhaps we should cache this
	Serializer s;
	mSignature.add(s);
	mMiddleTxn.add(s);
	mInnerTxn.add(s);
	return s.getSHA512Half();
}

std::vector<unsigned char> SerializedTransaction::getSignature() const
{
	return mSignature.getValue();
}

const std::vector<unsigned char>& SerializedTransaction::peekSignature() const
{
	return mSignature.peekValue();
}

bool SerializedTransaction::sign(CKey& key)
{
	return key.Sign(getSigningHash(), mSignature.peekValue());
}

bool SerializedTransaction::checkSign(const CKey& key) const
{
	return key.Verify(getSigningHash(), mSignature.getValue());
}

void SerializedTransaction::setSignature(const std::vector<unsigned char>& sig)
{
	mSignature.setValue(sig);
}

uint32 SerializedTransaction::getVersion() const
{
	const STUInt32* v = dynamic_cast<const STUInt32*>(mMiddleTxn.peekAtPIndex(TransactionIVersion));
	if (!v) throw std::runtime_error("corrupt transaction");
	return v->getValue();
}

void SerializedTransaction::setVersion(uint32 ver)
{
	STUInt32* v = dynamic_cast<STUInt32*>(mMiddleTxn.getPIndex(TransactionIVersion));
	if (!v) throw std::runtime_error("corrupt transaction");
	v->setValue(ver);
}

uint64 SerializedTransaction::getTransactionFee() const
{
	const STUInt64* v = dynamic_cast<const STUInt64*>(mMiddleTxn.peekAtPIndex(TransactionIFee));
	if (!v) throw std::runtime_error("corrupt transaction");
	return v->getValue();
}

void SerializedTransaction::setTransactionFee(uint64 fee)
{
	STUInt64* v = dynamic_cast<STUInt64*>(mMiddleTxn.getPIndex(TransactionIFee));
	if (!v) throw std::runtime_error("corrupt transaction");
	v->setValue(fee);
}

uint32 SerializedTransaction::getSequence() const
{
	const STUInt32* v = dynamic_cast<const STUInt32*>(mMiddleTxn.peekAtPIndex(TransactionISequence));
	if (!v) throw std::runtime_error("corrupt transaction");
	return v->getValue();
}

void SerializedTransaction::setSequence(uint32 seq)
{
	STUInt32* v = dynamic_cast<STUInt32*>(mMiddleTxn.getPIndex(TransactionISequence));
	if (!v) throw std::runtime_error("corrupt transaction");
	v->setValue(seq);
}

std::vector<unsigned char> SerializedTransaction::getRawSigningAccount() const
{
	const STVariableLength* v =
		dynamic_cast<const STVariableLength*>(mMiddleTxn.peekAtPIndex(TransactionISigningAccount));
	if (!v) throw std::runtime_error("corrupt transaction");
	return v->getValue();
}

const std::vector<unsigned char>& SerializedTransaction::peekRawSigningAccount() const
{
	const STVariableLength* v=
		dynamic_cast<const STVariableLength*>(mMiddleTxn.peekAtPIndex(TransactionISigningAccount));
	if (!v) throw std::runtime_error("corrupt transaction");
	return v->peekValue();
}

std::vector<unsigned char>& SerializedTransaction::peekRawSigningAccount()
{
	STVariableLength* v = dynamic_cast<STVariableLength*>(mMiddleTxn.getPIndex(TransactionISigningAccount));
	if (!v) throw std::runtime_error("corrupt transaction");
	return v->peekValue();
}

void SerializedTransaction::setSigningAccount(const std::vector<unsigned char>& s)
{
	STVariableLength* v = dynamic_cast<STVariableLength*>(mMiddleTxn.getPIndex(TransactionISigningAccount));
	if (!v) throw std::runtime_error("corrupt transaction");
	v->setValue(s);
	updateSigningAccount();
}

int SerializedTransaction::getITFieldIndex(SOE_Field field) const
{
	return mInnerTxn.getFieldIndex(field);
}

int SerializedTransaction::getITFieldCount() const
{
	return mInnerTxn.getCount();
}

bool SerializedTransaction::getITFieldPresent(SOE_Field field) const
{
	return mInnerTxn.isFieldPresent(field);
}

const SerializedType& SerializedTransaction::peekITField(SOE_Field field) const
{
	return mInnerTxn.peekAtField(field);
}

SerializedType& SerializedTransaction::getITField(SOE_Field field)
{
	return mInnerTxn.getField(field);
}

void SerializedTransaction::makeITFieldPresent(SOE_Field field)
{
	return mInnerTxn.makeFieldPresent(field);
}

void SerializedTransaction::makeITFieldAbsent(SOE_Field field)
{
	return mInnerTxn.makeFieldAbsent(field);
}

Json::Value SerializedTransaction::getJson(int options) const
{
	Json::Value ret(Json::objectValue);
	ret["Type"] = mFormat->t_name;
	ret["ID"] = getTransactionID().GetHex();
	ret["Signature"] = mSignature.getText();
	ret["Middle"] = mMiddleTxn.getJson(options);
	ret["Inner"] = mInnerTxn.getJson(options);
	return ret;
}
