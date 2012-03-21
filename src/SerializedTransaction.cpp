
#include "SerializedTransaction.h"

SerializedTransaction::SerializedTransaction(TransactionType type)
{
	mFormat=getFormat(type);
	if(mFormat==NULL) throw(std::runtime_error("invalid transaction type"));

	mMiddleTxn.giveObject(new STUInt32("Magic", TransactionMagic));
	mMiddleTxn.giveObject(new STVariableLength("Signature")); // signature
	mMiddleTxn.giveObject(new STUInt8("Type", static_cast<unsigned char>(type)));

	mInnerTxn=STUObject(mFormat->elements, "InnerTransaction");
}

SerializedTransaction::SerializedTransaction(SerializerIterator& sit, int length)
{
	if(length==0) length=sit.get32();
	if( (length<TransactionMinLen) || (length>TransactionMaxLen) )
		throw(std::runtime_error("Transaction length invalid"));

	mSignature.setValue(sit.getVL());

	if(sit.get32()!=TransactionMagic)
		throw(std::runtime_error("Transaction has invalid magic"));

	mMiddleTxn.giveObject(new STUInt32("Magic", TransactionMagic));
	mMiddleTxn.giveObject(new STVariableLength("Signature", sit.getVL()));

	int type=sit.get32();
	mMiddleTxn.giveObject(new STUInt32("Type", type));
	mFormat=getFormat(static_cast<TransactionType>(type));
	if(!mFormat) throw(std::runtime_error("Transaction has invalid type"));

	mInnerTxn=STUObject(mFormat->elements, sit, "InnerTransaction");
}

int SerializedTransaction::getLength() const
{
	return mSignature.getLength() + mMiddleTxn.getLength() + mInnerTxn.getLength();
}

std::string SerializedTransaction::getFullText() const
{ // WRITEME: Add transaction ID
	std::string ret="{";
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
	return ret;
}

void SerializedTransaction::add(Serializer& s) const
{
}
