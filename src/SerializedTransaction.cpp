
#include "SerializedTransaction.h"

SerializedTransaction::SerializedTransaction(TransactionType type)
{
	mFormat=getFormat(type);
	if(mFormat==NULL) throw(std::runtime_error("invalid transaction type"));

	mMiddleTxn.giveObject(new STUInt32("Magic", TransactionMagic));
	mMiddleTxn.giveObject(new STVariableLength("SigningAccount"));
	mMiddleTxn.giveObject(new STUInt8("Type", static_cast<unsigned char>(type)));
	mMiddleTxn.giveObject(new STUInt64("Fee"));

	mInnerTxn=STObject(mFormat->elements, "InnerTransaction");
}

SerializedTransaction::SerializedTransaction(SerializerIterator& sit, int length)
{
	if(length==-1) length=sit.getBytesLeft();
	else if(length==0) length=sit.get32();
	if( (length<TransactionMinLen) || (length>TransactionMaxLen) )
		throw(std::runtime_error("Transaction length invalid"));

	mSignature.setValue(sit.getVL());

	if(sit.get32()!=TransactionMagic)
		throw(std::runtime_error("Transaction has invalid magic"));

	mMiddleTxn.giveObject(new STUInt32("Magic", TransactionMagic));
	mMiddleTxn.giveObject(new STVariableLength("SigningAccount", sit.getVL()));

	int type=sit.get32();
	mMiddleTxn.giveObject(new STUInt32("Type", type));
	mFormat=getFormat(static_cast<TransactionType>(type));
	if(!mFormat) throw(std::runtime_error("Transaction has invalid type"));
	mMiddleTxn.giveObject(new STUInt64("Fee", sit.get64()));

	mInnerTxn=STObject(mFormat->elements, sit, "InnerTransaction");
}

int SerializedTransaction::getLength() const
{
	return mSignature.getLength() + mMiddleTxn.getLength() + mInnerTxn.getLength();
}

std::string SerializedTransaction::getFullText() const
{
	std::string ret="\"";
	ret+=getTransactionID().GetHex();
	ret+="\" = {";
	ret+=mSignature.getFullText();
	ret+=mMiddleTxn.getFullText();
	ret+=mInnerTxn.getFullText();
	ret+="}";
	return ret;
}

std::string SerializedTransaction::getText() const
{
	std::string ret="{";
	ret+=mSignature.getText();
	ret+=mMiddleTxn.getText();
	ret+=mInnerTxn.getText();
	ret+="}";
	return ret;
}

int SerializedTransaction::getTransaction(Serializer& s, bool include_length) const
{
	int l=getLength();
	if(include_length) s.add32(l);
	mSignature.add(s);
	mMiddleTxn.add(s);
	mInnerTxn.add(s);
	return l;
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

void SerializedTransaction::setSignature(const std::vector<unsigned char>& sig)
{
	mSignature.setValue(sig);
}

uint32 SerializedTransaction::getVersion() const
{
	const STUInt32* v=dynamic_cast<const STUInt32*>(mMiddleTxn.peekAtPIndex(0));
	if(!v) throw(std::runtime_error("corrupt transaction"));
	return v->getValue();
}

void SerializedTransaction::setVersion(uint32 ver)
{
	STUInt32* v=dynamic_cast<STUInt32*>(mMiddleTxn.getPIndex(0));
	if(!v) throw(std::runtime_error("corrupt transaction"));
	v->setValue(ver);
}

uint64 SerializedTransaction::getTransactionFee() const
{
	const STUInt64* v=dynamic_cast<const STUInt64*>(mMiddleTxn.peekAtPIndex(3));
	if(!v) throw(std::runtime_error("corrupt transaction"));
	return v->getValue();
}

void SerializedTransaction::setTransactionFee(uint64 fee)
{
	STUInt64* v=dynamic_cast<STUInt64*>(mMiddleTxn.getPIndex(3));
	if(!v) throw(std::runtime_error("corrupt transaction"));
	v->setValue(fee);
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

const SerializedType& SerializedTransaction::peekITField(SOE_Field field)
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
 