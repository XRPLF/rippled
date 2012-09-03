
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include "SerializedTypes.h"
#include "SerializedObject.h"
#include "TransactionFormats.h"
#include "Log.h"
#include "NewcoinAddress.h"
#include "utils.h"

std::string SerializedType::getFullText() const
{
	std::string ret;
	if (getSType() != STI_NOTPRESENT)
	{
		if(name != NULL)
		{
			ret = name;
			ret += " = ";
		}
		ret += getText();
	}
	return ret;
}

STUInt8* STUInt8::construct(SerializerIterator& u, const char *name)
{
	return new STUInt8(name, u.get8());
}

std::string STUInt8::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

bool STUInt8::isEquivalent(const SerializedType& t) const
{
	const STUInt8* v = dynamic_cast<const STUInt8*>(&t);
	return v && (value == v->value);
}

STUInt16* STUInt16::construct(SerializerIterator& u, const char *name)
{
	return new STUInt16(name, u.get16());
}

std::string STUInt16::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

bool STUInt16::isEquivalent(const SerializedType& t) const
{
	const STUInt16* v = dynamic_cast<const STUInt16*>(&t);
	return v && (value == v->value);
}

STUInt32* STUInt32::construct(SerializerIterator& u, const char *name)
 {
	return new STUInt32(name, u.get32());
}

std::string STUInt32::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

bool STUInt32::isEquivalent(const SerializedType& t) const
{
	const STUInt32* v = dynamic_cast<const STUInt32*>(&t);
	return v && (value == v->value);
}

STUInt64* STUInt64::construct(SerializerIterator& u, const char *name)
{
	return new STUInt64(name, u.get64());
}

std::string STUInt64::getText() const
{
	return boost::lexical_cast<std::string>(value);
}

bool STUInt64::isEquivalent(const SerializedType& t) const
{
	const STUInt64* v = dynamic_cast<const STUInt64*>(&t);
	return v && (value == v->value);
}

STHash128* STHash128::construct(SerializerIterator& u, const char *name)
{
	return new STHash128(name, u.get128());
}

std::string STHash128::getText() const
{
	return value.GetHex();
}

bool STHash128::isEquivalent(const SerializedType& t) const
{
	const STHash128* v = dynamic_cast<const STHash128*>(&t);
	return v && (value == v->value);
}

STHash160* STHash160::construct(SerializerIterator& u, const char *name)
{
	return new STHash160(name, u.get160());
}

std::string STHash160::getText() const
{
	return value.GetHex();
}

bool STHash160::isEquivalent(const SerializedType& t) const
{
	const STHash160* v = dynamic_cast<const STHash160*>(&t);
	return v && (value == v->value);
}

STHash256* STHash256::construct(SerializerIterator& u, const char *name)
{
	return new STHash256(name, u.get256());
}

std::string STHash256::getText() const
{
	return value.GetHex();
}

bool STHash256::isEquivalent(const SerializedType& t) const
{
	const STHash256* v = dynamic_cast<const STHash256*>(&t);
	return v && (value == v->value);
}

STVariableLength::STVariableLength(SerializerIterator& st, const char *name) : SerializedType(name)
{
	value = st.getVL();
}

std::string STVariableLength::getText() const
{
	return strHex(value);
}

STVariableLength* STVariableLength::construct(SerializerIterator& u, const char *name)
{
	return new STVariableLength(name, u.getVL());
}

int STVariableLength::getLength() const
{
	return Serializer::encodeLengthLength(value.size()) + value.size();
}

bool STVariableLength::isEquivalent(const SerializedType& t) const
{
	const STVariableLength* v = dynamic_cast<const STVariableLength*>(&t);
	return v && (value == v->value);
}

std::string STAccount::getText() const
{
	uint160 u;
	NewcoinAddress a;

	if (!getValueH160(u))
		return STVariableLength::getText();
	a.setAccountID(u);
	return a.humanAccountID();
}

STAccount* STAccount::construct(SerializerIterator& u, const char *name)
{
	return new STAccount(name, u.getVL());
}

//
// STVector256
//

// Return a new object from a SerializerIterator.
STVector256* STVector256::construct(SerializerIterator& u, const char *name)
{
	std::vector<unsigned char> data = u.getVL();
	std::vector<uint256> value;

	int count = data.size() / (256 / 8);
	value.reserve(count);

	unsigned int	uStart	= 0;
	for (unsigned int i = 0; i != count; i++)
	{
		unsigned int	uEnd	= uStart+(256/8);

		value.push_back(uint256(std::vector<unsigned char>(data.begin()+uStart, data.begin()+(uStart+32))));

		uStart	= uEnd;
	}

	return new STVector256(name, value);
}

void STVector256::add(Serializer& s) const
{
	s.addVL(mValue.empty() ? NULL : mValue[0].begin(), mValue.size() * (256 / 8));
}

bool STVector256::isEquivalent(const SerializedType& t) const
{
	const STVector256* v = dynamic_cast<const STVector256*>(&t);
	return v && (mValue == v->mValue);
}

//
// STAccount
//

bool STAccount::isValueH160() const
{
	return peekValue().size() == (160/8);
}

void STAccount::setValueH160(const uint160& v)
{
	peekValue().clear();
	peekValue().insert(peekValue().end(), v.begin(), v.end());
	assert(peekValue().size() == (160/8));
}

bool STAccount::getValueH160(uint160& v) const
{
	if (!isValueH160()) return false;
	memcpy(v.begin(), &(peekValue().front()), (160/8));
	return true;
}

NewcoinAddress STAccount::getValueNCA() const
{
	NewcoinAddress a;
	uint160 v;
	if (getValueH160(v))
		a.setAccountID(v);
	return a;
}

void STAccount::setValueNCA(const NewcoinAddress& nca)
{
	setValueH160(nca.getAccountID());
}

std::string STTaggedList::getText() const
{
	std::string ret;
	for (std::vector<TaggedListItem>::const_iterator it=value.begin(); it!=value.end(); ++it)
	{
		ret += boost::lexical_cast<std::string>(it->first);
		ret += ",";
		ret += strHex(it->second);
	}
	return ret;
}

Json::Value STTaggedList::getJson(int) const
{
	Json::Value ret(Json::arrayValue);

	for (std::vector<TaggedListItem>::const_iterator it=value.begin(); it!=value.end(); ++it)
	{
		Json::Value elem(Json::arrayValue);
		elem.append(it->first);
		elem.append(strHex(it->second));
		ret.append(elem);
	}

	return ret;
}

STTaggedList* STTaggedList::construct(SerializerIterator& u, const char *name)
{
	return new STTaggedList(name, u.getTaggedList());
}

int STTaggedList::getLength() const
{
	int ret = Serializer::getTaggedListLength(value);
	if (ret<0) throw std::overflow_error("bad TL length");
	return ret;
}

bool STTaggedList::isEquivalent(const SerializedType& t) const
{
	const STTaggedList* v = dynamic_cast<const STTaggedList*>(&t);
	return v && (value == v->value);
}

STPathSet* STPathSet::construct(SerializerIterator& s, const char *name)
{
	std::vector<STPath> paths;
	std::vector<STPathElement> path;

	do
	{
		int	iType	= s.get8();

		if (iType == STPathElement::typeEnd || iType == STPathElement::typeBoundary)
		{
			if (path.empty())
			{
				Log(lsINFO) << "STPathSet: Empty path.";

				throw std::runtime_error("empty path");
			}

			paths.push_back(path);
			path.clear();

			if (iType == STPathElement::typeEnd)
			{
				return new STPathSet(name, paths);
			}
		}
		else if (iType & ~STPathElement::typeValidBits)
		{
			Log(lsINFO) << "STPathSet: Bad path element: " << iType;

			throw std::runtime_error("bad path element");
		}
		else
		{
			const bool	bAccount	= !!(iType & STPathElement::typeAccount);
			const bool	bCurrency	= !!(iType & STPathElement::typeCurrency);
			const bool	bIssuer		= !!(iType & STPathElement::typeIssuer);

			uint160	uAccountID;
			uint160	uCurrency;
			uint160	uIssuerID;

			if (bAccount)
				uAccountID	= s.get160();

			if (bCurrency)
				uCurrency	= s.get160();

			if (bIssuer)
				uIssuerID	= s.get160();

			path.push_back(STPathElement(uAccountID, uCurrency, uIssuerID));
		}
	} while(1);
}

bool STPathSet::isEquivalent(const SerializedType& t) const
{
	const STPathSet* v = dynamic_cast<const STPathSet*>(&t);
	return v && (value == v->value);
}

int STPath::getSerializeSize() const
{
	int iBytes = 0;

	BOOST_FOREACH(const STPathElement& speElement, mPath)
	{
		int	iType	= speElement.getNodeType();

		iBytes	+= 1;	// mType

		if (iType & STPathElement::typeAccount)
			iBytes	+= 160/8;

		if (iType & STPathElement::typeCurrency)
			iBytes	+= 160/8;

		if (iType & STPathElement::typeIssuer)
			iBytes	+= 160/8;
	}

	iBytes	+= 1;	// typeBoundary | typeEnd

	return iBytes;
}

int STPathSet::getLength() const
{
	int iBytes = 0;

	BOOST_FOREACH(const STPath& spPath, value)
	{
		iBytes += spPath.getSerializeSize();
	}

	return iBytes ? iBytes : 1;
}

Json::Value STPath::getJson(int) const
{
	Json::Value ret(Json::arrayValue);

	BOOST_FOREACH(std::vector<STPathElement>::const_iterator::value_type it, mPath)
	{
		Json::Value elem(Json::objectValue);
		int			iType	= it.getNodeType();

		elem["type"]		= it.getNodeType();
		elem["type_hex"]	= strHex(it.getNodeType());

		if (iType & STPathElement::typeAccount)
			elem["account"]		= NewcoinAddress::createHumanAccountID(it.getAccountID());

		if (iType & STPathElement::typeCurrency)
			elem["currency"]	= STAmount::createHumanCurrency(it.getCurrency());

		if (iType & STPathElement::typeIssuer)
			elem["issuer"]		= NewcoinAddress::createHumanAccountID(it.getIssuerID());

		ret.append(elem);
	}

	return ret;
}

Json::Value STPathSet::getJson(int options) const
{
	Json::Value ret(Json::arrayValue);

	BOOST_FOREACH(std::vector<STPath>::const_iterator::value_type it, value)
		ret.append(it.getJson(options));

	return ret;
}

#if 0
std::string STPath::getText() const
{
	std::string ret("[");
	bool first = true;

	BOOST_FOREACH(const STPathElement& it, mPath)
	{
		if (!first) ret += ", ";
		switch (it.getNodeType())
		{
			case STPathElement::typeAccount:
			{
				ret += NewcoinAddress::createHumanAccountID(it.getNode());
				break;
			}

			case STPathElement::typeOffer:
			{
				ret += "Offer(";
				ret += it.getNode().GetHex();
				ret += ")";
				break;
			}

			default: throw std::runtime_error("Unknown path element");
		}
		first = false;
	}

	return ret + "]";
}
#endif

#if 0
std::string STPathSet::getText() const
{
	std::string ret("{");
	bool firstPath = true;

	BOOST_FOREACH(std::vector<STPath>::const_iterator::value_type it, value)
	{
		if (!firstPath)
		{
			ret += ", ";
			firstPath = false;
		}
		ret += it.getText();
	}
	return ret + "}";
}
#endif

void STPathSet::add(Serializer& s) const
{
	bool bFirst = true;

	BOOST_FOREACH(const STPath& spPath, value)
	{
		if (!bFirst)
		{
			s.add8(STPathElement::typeBoundary);
			bFirst = false;
		}

		BOOST_FOREACH(const STPathElement& speElement, spPath)
		{
			int		iType	= speElement.getNodeType();

			s.add8(iType);

			if (iType & STPathElement::typeAccount)
				s.add160(speElement.getAccountID());

			if (iType & STPathElement::typeCurrency)
				s.add160(speElement.getCurrency());

			if (iType & STPathElement::typeIssuer)
				s.add160(speElement.getIssuerID());
		}
	}
	s.add8(STPathElement::typeEnd);
}
// vim:ts=4
