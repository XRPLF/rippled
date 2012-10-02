
#include "SerializedObject.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include "../json/writer.h"

#include "Log.h"
#include "LedgerFormats.h"
#include "TransactionFormats.h"

std::auto_ptr<SerializedType> STObject::makeDefaultObject(SerializedTypeID id, SField::ref name)
{
	assert((id == STI_NOTPRESENT) || (id == name.fieldType));

	switch(id)
	{
		case STI_NOTPRESENT:
			return std::auto_ptr<SerializedType>(new SerializedType(name));

		case STI_UINT16:
			return std::auto_ptr<SerializedType>(new STUInt16(name));

		case STI_UINT32:
			return std::auto_ptr<SerializedType>(new STUInt32(name));

		case STI_UINT64:
			return std::auto_ptr<SerializedType>(new STUInt64(name));

		case STI_AMOUNT:
			return std::auto_ptr<SerializedType>(new STAmount(name));

		case STI_HASH128:
			return std::auto_ptr<SerializedType>(new STHash128(name));

		case STI_HASH160:
			return std::auto_ptr<SerializedType>(new STHash160(name));

		case STI_HASH256:
			return std::auto_ptr<SerializedType>(new STHash256(name));

		case STI_VECTOR256:
			return std::auto_ptr<SerializedType>(new STVector256(name));

		case STI_VL:
			return std::auto_ptr<SerializedType>(new STVariableLength(name));

		case STI_ACCOUNT:
			return std::auto_ptr<SerializedType>(new STAccount(name));

		case STI_PATHSET:
			return std::auto_ptr<SerializedType>(new STPathSet(name));

		case STI_OBJECT:
			return std::auto_ptr<SerializedType>(new STObject(name));

		case STI_ARRAY:
			return std::auto_ptr<SerializedType>(new STArray(name));

		default:
			throw std::runtime_error("Unknown object type");
	}
}

std::auto_ptr<SerializedType> STObject::makeDeserializedObject(SerializedTypeID id, SField::ref name,
	SerializerIterator& sit, int depth)
{
	switch(id)
	{
		case STI_NOTPRESENT:
			return SerializedType::deserialize(name);

		case STI_UINT16:
			return STUInt16::deserialize(sit, name);

		case STI_UINT32:
			return STUInt32::deserialize(sit, name);

		case STI_UINT64:
			return STUInt64::deserialize(sit, name);

		case STI_AMOUNT:
			return STAmount::deserialize(sit, name);

		case STI_HASH128:
			return STHash128::deserialize(sit, name);

		case STI_HASH160:
			return STHash160::deserialize(sit, name);

		case STI_HASH256:
			return STHash256::deserialize(sit, name);

		case STI_VECTOR256:
			return STVector256::deserialize(sit, name);

		case STI_VL:
			return STVariableLength::deserialize(sit, name);

		case STI_ACCOUNT:
			return STAccount::deserialize(sit, name);

		case STI_PATHSET:
			return STPathSet::deserialize(sit, name);

		case STI_ARRAY:
			return STArray::deserialize(sit, name);

		case STI_OBJECT:
			return STObject::deserialize(sit, name);

		default:
			throw std::runtime_error("Unknown object type");
	}
}

void STObject::set(const std::vector<SOElement::ptr>& type)
{
	mData.empty();
	mType.empty();

	BOOST_FOREACH(const SOElement::ptr& elem, type)
	{
		mType.push_back(elem);
		if (elem->flags == SOE_OPTIONAL)
			giveObject(makeNonPresentObject(elem->e_field));
		else
			giveObject(makeDefaultObject(elem->e_field));
	}
}

bool STObject::setType(const std::vector<SOElement::ptr> &type)
{
	boost::ptr_vector<SerializedType> newData;
	bool valid = true;

	mType.empty();
	BOOST_FOREACH(const SOElement::ptr& elem, type)
	{
		bool match = false;
		for (boost::ptr_vector<SerializedType>::iterator it = mData.begin(); it != mData.end(); ++it)
			if (it->getFName() == elem->e_field)
			{
				match = true;
				newData.push_back(mData.release(it).release());
				break;
			}

		if (!match)
		{
			if (elem->flags != SOE_OPTIONAL)
			{
				Log(lsTRACE) << "setType !valid missing";
				valid = false;
			}
			newData.push_back(makeNonPresentObject(elem->e_field));
		}

		mType.push_back(elem);
	}
	if (mData.size() != 0)
	{
		Log(lsTRACE) << "setType !valid leftover";
		valid = false;
	}
	mData.swap(newData);
	return valid;
}

bool STObject::isValidForType()
{
	boost::ptr_vector<SerializedType>::iterator it = mData.begin();
	BOOST_FOREACH(SOElement::ptr elem, mType)
	{
		if (it == mData.end())
			return false;
		if (elem->e_field != it->getFName())
			return false;
		++it;
	}

	return true;
}

bool STObject::isFieldAllowed(SField::ref field)
{
	BOOST_FOREACH(SOElement::ptr elem, mType)
	{ // are any required elemnents missing
		if (elem->e_field == field)
			return true;
	}
	return false;
}

bool STObject::set(SerializerIterator& sit, int depth)
{ // return true = terminated with end-of-object
	mData.empty();
	while (!sit.empty())
	{
		int type, field;
		sit.getFieldID(type, field);
		if ((type == STI_OBJECT) && (field == 1)) // end of object indicator
			return true;
		SField::ref fn = SField::getField(type, field);
		if (fn.isInvalid())
			throw std::runtime_error("Unknown field");
		giveObject(makeDeserializedObject(fn.fieldType, fn, sit, depth + 1));
	}
	return false;
}

std::auto_ptr<SerializedType> STObject::deserialize(SerializerIterator& sit, SField::ref name)
{
	STObject *o;
	std::auto_ptr<SerializedType> object(o = new STObject(name));
	o->set(sit, 1);
	return object;
}

std::string STObject::getFullText() const
{
	std::string ret;
	bool first = true;
	if (fName->hasName())
	{
		ret = fName->getName();
		ret += " = {";
	}
	else ret = "{";

	BOOST_FOREACH(const SerializedType& it, mData)
		if (it.getSType() != STI_NOTPRESENT)
		{
			if (!first) ret += ", ";
			else first = false;
			ret += it.getFullText();
		}

	ret += "}";
	return ret;
}

void STObject::add(Serializer& s, bool withSigningFields) const
{
	std::map<int, const SerializedType*> fields;

	BOOST_FOREACH(const SerializedType& it, mData)
	{ // pick out the fields and sort them
		if (it.getSType() != STI_NOTPRESENT)
		{
			SField::ref fName = it.getFName();
			if (withSigningFields || ((fName != sfTxnSignature) && (fName != sfTxnSignatures)))
				fields.insert(std::make_pair(it.getFName().fieldCode, &it));
		}
	}


	typedef std::pair<const int, const SerializedType*> field_iterator;
	BOOST_FOREACH(field_iterator& it, fields)
	{ // insert them in sorted order
		const SerializedType* field = it.second;

		field->addFieldID(s);
		field->add(s);
		if (dynamic_cast<const STArray*>(field) != NULL)
			s.addFieldID(STI_ARRAY, 1);
		else if (dynamic_cast<const STObject*>(field) != NULL)
			s.addFieldID(STI_OBJECT, 1);
	}
}

std::string STObject::getText() const
{
	std::string ret = "{";
	bool first = false;
	BOOST_FOREACH(const SerializedType& it, mData)
	{
		if (!first)
		{
			ret += ", ";
			first = false;
		}
		ret += it.getText();
	}
	ret += "}";
	return ret;
}

bool STObject::isEquivalent(const SerializedType& t) const
{
	const STObject* v = dynamic_cast<const STObject*>(&t);
	if (!v)
		return false;
	boost::ptr_vector<SerializedType>::const_iterator it1 = mData.begin(), end1 = mData.end();
	boost::ptr_vector<SerializedType>::const_iterator it2 = v->mData.begin(), end2 = v->mData.end();
	while ((it1 != end1) && (it2 != end2))
	{
		if ((it1->getSType() != it2->getSType()) || !it1->isEquivalent(*it2))
			return false;
		++it1;
		++it2;
	}
	return (it1 == end1) && (it2 == end2);
}

uint256 STObject::getHash(uint32 prefix) const
{
	Serializer s;
	s.add32(prefix);
	add(s, true);
	return s.getSHA512Half();
}

uint256 STObject::getSigningHash(uint32 prefix) const
{
	Serializer s;
	s.add32(prefix);
	add(s, false);
	return s.getSHA512Half();
}

int STObject::getFieldIndex(SField::ref field) const
{
	int i = 0;
	BOOST_FOREACH(const SerializedType& elem, mData)
	{
		if (elem.getFName() == field)
			return i;
		++i;
	}
	return -1;
}

const SerializedType& STObject::peekAtField(SField::ref field) const
{
	int index = getFieldIndex(field);
	if (index == -1)
		throw std::runtime_error("Field not found");
	return peekAtIndex(index);
}

SerializedType& STObject::getField(SField::ref field)
{
	int index = getFieldIndex(field);
	if (index == -1)
		throw std::runtime_error("Field not found");
	return getIndex(index);
}

SField::ref STObject::getFieldSType(int index) const
{
	return mData[index].getFName();
}

const SerializedType* STObject::peekAtPField(SField::ref field) const
{
	int index = getFieldIndex(field);
	if (index == -1)
		return NULL;
	return peekAtPIndex(index);
}

SerializedType* STObject::getPField(SField::ref field)
{
	int index = getFieldIndex(field);
	if (index == -1)
		return NULL;
	return getPIndex(index);
}

bool STObject::isFieldPresent(SField::ref field) const
{
	int index = getFieldIndex(field);
	if (index == -1)
		return false;
	return peekAtIndex(index).getSType() != STI_NOTPRESENT;
}

bool STObject::setFlag(uint32 f)
{
	STUInt32* t = dynamic_cast<STUInt32*>(getPField(sfFlags));
	if (!t)
		return false;
	t->setValue(t->getValue() | f);
	return true;
}

bool STObject::clearFlag(uint32 f)
{
	STUInt32* t = dynamic_cast<STUInt32*>(getPField(sfFlags));
	if (!t)
		return false;
	t->setValue(t->getValue() & ~f);
	return true;
}

uint32 STObject::getFlags(void) const
{
	const STUInt32* t = dynamic_cast<const STUInt32*>(peekAtPField(sfFlags));
	if (!t)
		return 0;
	return t->getValue();
}

SerializedType* STObject::makeFieldPresent(SField::ref field)
{
	int index = getFieldIndex(field);
	if (index == -1)
		throw std::runtime_error("Field not found");

	SerializedType* f = getPIndex(index);
	if (f->getSType() != STI_NOTPRESENT)
		return f;
	mData.replace(index, makeDefaultObject(f->getFName()));
	return getPIndex(index);
}

void STObject::makeFieldAbsent(SField::ref field)
{
	int index = getFieldIndex(field);
	if (index == -1)
		throw std::runtime_error("Field not found");

	const SerializedType& f = peekAtIndex(index);
	if (f.getSType() == STI_NOTPRESENT)
		return;

	mData.replace(index, makeDefaultObject(f.getFName()));
}

bool STObject::delField(SField::ref field)
{
	int index = getFieldIndex(field);
	if (index == -1)
		return false;
	delField(index);
	return true;
}

void STObject::delField(int index)
{
	mData.erase(mData.begin() + index);
}

std::string STObject::getFieldString(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	return rf->getText();
}

unsigned char STObject::getFieldU8(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt8 *cf = dynamic_cast<const STUInt8 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint16 STObject::getFieldU16(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt16 *cf = dynamic_cast<const STUInt16 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint32 STObject::getFieldU32(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt32 *cf = dynamic_cast<const STUInt32 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint64 STObject::getFieldU64(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt64 *cf = dynamic_cast<const STUInt64 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint128 STObject::getFieldH128(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return uint128(); // optional field not present
	const STHash128 *cf = dynamic_cast<const STHash128 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint160 STObject::getFieldH160(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return uint160(); // optional field not present
	const STHash160 *cf = dynamic_cast<const STHash160 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint256 STObject::getFieldH256(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return uint256(); // optional field not present
	const STHash256 *cf = dynamic_cast<const STHash256 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

NewcoinAddress STObject::getFieldAccount(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf)
	{
#ifdef DEBUG
		std::cerr << "Account field not found" << std::endl;
		std::cerr << getFullText() << std::endl;
#endif
		throw std::runtime_error("Field not found");
	}
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return NewcoinAddress(); // optional field not present
	const STAccount* cf = dynamic_cast<const STAccount *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValueNCA();
}

uint160 STObject::getFieldAccount160(SField::ref field) const
{
	uint160 a;
	const SerializedType* rf = peekAtPField(field);
	if (!rf)
	{
#ifdef DEBUG
		std::cerr << "Account field not found" << std::endl;
		std::cerr << getFullText() << std::endl;
#endif
		throw std::runtime_error("Field not found");
	}
	SerializedTypeID id = rf->getSType();
	if (id != STI_NOTPRESENT)
	{
		const STAccount* cf = dynamic_cast<const STAccount *>(rf);
		if (!cf) throw std::runtime_error("Wrong field type");
		cf->getValueH160(a);
	}
	return a;
}

std::vector<unsigned char> STObject::getFieldVL(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return std::vector<unsigned char>(); // optional field not present
	const STVariableLength *cf = dynamic_cast<const STVariableLength *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

STAmount STObject::getFieldAmount(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return STAmount(); // optional field not present
	const STAmount *cf = dynamic_cast<const STAmount *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return *cf;
}

STPathSet STObject::getFieldPathSet(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return STPathSet(); // optional field not present
	const STPathSet *cf = dynamic_cast<const STPathSet *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return *cf;
}

STVector256 STObject::getFieldV256(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return STVector256(); // optional field not present
	const STVector256 *cf = dynamic_cast<const STVector256 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return *cf;
}

void STObject::setFieldU8(SField::ref field, unsigned char v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt8* cf = dynamic_cast<STUInt8*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldU16(SField::ref field, uint16 v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt16* cf = dynamic_cast<STUInt16*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldU32(SField::ref field, uint32 v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt32* cf = dynamic_cast<STUInt32*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldU64(SField::ref field, uint64 v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt64* cf = dynamic_cast<STUInt64*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldH128(SField::ref field, const uint128& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STHash128* cf = dynamic_cast<STHash128*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldH160(SField::ref field, const uint160& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STHash160* cf = dynamic_cast<STHash160*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldH256(SField::ref field, const uint256& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STHash256* cf = dynamic_cast<STHash256*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldV256(SField::ref field, const STVector256& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STVector256* cf = dynamic_cast<STVector256*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldAccount(SField::ref field, const uint160& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STAccount* cf = dynamic_cast<STAccount*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValueH160(v);
}

void STObject::setFieldVL(SField::ref field, const std::vector<unsigned char>& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STVariableLength* cf = dynamic_cast<STVariableLength*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldAmount(SField::ref field, const STAmount &v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STAmount* cf = dynamic_cast<STAmount*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	(*cf) = v;
}

void STObject::setFieldPathSet(SField::ref field, const STPathSet &v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STPathSet* cf = dynamic_cast<STPathSet*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	(*cf) = v;
}

Json::Value STObject::getJson(int options) const
{
	Json::Value ret(Json::objectValue);
	int index = 1;
	BOOST_FOREACH(const SerializedType& it, mData)
	{
		if (it.getSType() != STI_NOTPRESENT)
		{
			if (!it.getFName().hasName())
				ret[lexical_cast_i(index)] = it.getJson(options);
			else
				ret[it.getName()] = it.getJson(options);
		}
	}
	return ret;
}

Json::Value STVector256::getJson(int options) const
{
	Json::Value ret(Json::arrayValue);

	BOOST_FOREACH(std::vector<uint256>::const_iterator::value_type vEntry, mValue)
		ret.append(vEntry.ToString());

	return ret;
}

std::string STArray::getFullText() const
{
	return "WRITEME";
}

std::string STArray::getText() const
{
	return "WRITEME";
}

Json::Value STArray::getJson(int) const
{
	return Json::Value("WRITEME");
}

void STArray::add(Serializer& s) const
{
	BOOST_FOREACH(const STObject& object, value)
	{
		object.addFieldID(s);
		object.add(s);
		s.addFieldID(STI_OBJECT, 1);
	}
}

bool STArray::isEquivalent(const SerializedType& t) const
{
	const STArray* v = dynamic_cast<const STArray*>(&t);
	if (!v)
		return false;
	return value == v->value;
}

STArray* STArray::construct(SerializerIterator& sit, SField::ref field)
{
	vector value;

	while (!sit.empty())
	{
		int type, field;
		sit.getFieldID(type, field);
		if ((type == STI_ARRAY) && (field == 1))
			break;

		SField::ref fn = SField::getField(type, field);
		if (fn.isInvalid())
			throw std::runtime_error("Unknown field");

		value.push_back(STObject(fn));
		value.rbegin()->set(sit, 1);
	}

	return new STArray(field, value);
}

std::auto_ptr<STObject> STObject::parseJson(const Json::Value& object, SField::ref inName, int depth)
{
	if (!object.isObject())
		throw std::runtime_error("Value is not an object");

	SField::ptr name = &inName;

	boost::ptr_vector<SerializedType> data;
	Json::Value::Members members(object.getMemberNames());
	for (Json::Value::Members::iterator it = members.begin(), end = members.end(); it != end; ++it)
	{
		const std::string& fieldName = *it;
		const Json::Value& value = object[fieldName];

		SField::ref field = SField::getField(fieldName);
		if (field == sfInvalid)
			throw std::runtime_error("Unknown field: " + fieldName);

		switch (field.fieldType)
		{
			case STI_UINT8:
				if (value.isString())
					data.push_back(new STUInt8(field, lexical_cast_s<unsigned char>(value.asString())));
				else if (value.isInt())	
				{
					if (value.asInt() < 0 || value.asInt() > 255)
						throw std::runtime_error("value out of rand");
					data.push_back(new STUInt8(field, static_cast<unsigned char>(value.asInt())));
				}
				else if (value.isUInt())
				{
					if (value.asUInt() > 255)
						throw std::runtime_error("value out of rand");
					data.push_back(new STUInt8(field, static_cast<unsigned char>(value.asUInt())));
				}
				else
					throw std::runtime_error("Incorrect type");
				break;

			case STI_UINT16:
				if (value.isString())
				{
					std::string strValue = value.asString();
					if (!strValue.empty() && ((strValue[0] < '0') || (strValue[0] > '9')))
					{
						if (field == sfTransactionType)
						{
							TransactionFormat* f = TransactionFormat::getTxnFormat(strValue);
							if (!f)
								throw std::runtime_error("Unknown transaction type");
							data.push_back(new STUInt16(field, static_cast<uint16>(f->t_type)));
							if (*name == sfGeneric)
								name = &sfTransaction;
						}
						else if (field == sfLedgerEntryType)
						{
							LedgerEntryFormat* f = LedgerEntryFormat::getLgrFormat(strValue);
							if (!f)
								throw std::runtime_error("Unknown ledger entry type");
							data.push_back(new STUInt16(field, static_cast<uint16>(f->t_type)));
							if (*name == sfGeneric)
								name = &sfLedgerEntry;
						}
						else
							throw std::runtime_error("Invalid field data");
					}
					else
						data.push_back(new STUInt16(field, lexical_cast_s<uint16>(strValue)));
				}
				else if (value.isInt())
					data.push_back(new STUInt16(field, static_cast<uint16>(value.asInt())));
				else if (value.isUInt())
					data.push_back(new STUInt16(field, static_cast<uint16>(value.asUInt())));
				else
					throw std::runtime_error("Incorrect type");
				break;

			case STI_UINT32:
				if (value.isString())
					data.push_back(new STUInt32(field, lexical_cast_s<uint32>(value.asString())));
				else if (value.isInt())
					data.push_back(new STUInt32(field, static_cast<uint32>(value.asInt())));
				else if (value.isUInt())
					data.push_back(new STUInt32(field, static_cast<uint32>(value.asUInt())));
				else
					throw std::runtime_error("Incorrect type");
				break;

			case STI_UINT64:
				if (value.isString())
					data.push_back(new STUInt64(field, lexical_cast_s<uint64>(value.asString())));
				else if (value.isInt())
					data.push_back(new STUInt64(field, static_cast<uint64>(value.asInt())));
				else if (value.isUInt())
					data.push_back(new STUInt64(field, static_cast<uint64>(value.asUInt())));
				else
					throw std::runtime_error("Incorrect type");
				break;


			case STI_HASH128:
				if (value.isString())
					data.push_back(new STHash128(field, value.asString()));
				else
					throw std::runtime_error("Incorrect type");
				break;

			case STI_HASH160:
				if (value.isString())
					data.push_back(new STHash160(field, value.asString()));
				else
					throw std::runtime_error("Incorrect type");
				break;

			case STI_HASH256:
				if (value.isString())
					data.push_back(new STHash256(field, value.asString()));
				else
					throw std::runtime_error("Incorrect type");
				break;

			case STI_VL:
			{
				// WRITEME
			}
			break;

			case STI_AMOUNT:
				// WRITEME

			case STI_VECTOR256:
				// WRITEME

			case STI_PATHSET:
				// WRITEME

			case STI_OBJECT:
			case STI_ACCOUNT:
			case STI_TRANSACTION:
			case STI_LEDGERENTRY:
			case STI_VALIDATION:
				if (!value.isObject())
					throw std::runtime_error("Inner value is not an object");
				if (depth > 64)	
					throw std::runtime_error("Json nest depth exceeded");
				data.push_back(parseJson(value, field, depth + 1));
				break;

			case STI_ARRAY:
				// WRITEME

			default:
				throw std::runtime_error("Invalid field type");
		}
	}
	return std::auto_ptr<STObject>(new STObject(*name, data));
}

#if 0

static SOElement testSOElements[2][16] =
{ // field, name, id, type, flags
	{
		{ sfFlags, "Flags", STI_UINT32,		SOE_FLAGS, 0 },
		{ sfTest1, "Test1", STI_VL,			SOE_REQUIRED, 0 },
		{ sfTest2, "Test2", STI_HASH256,	SOE_IFFLAG, 1 },
		{ sfTest3, "Test3", STI_UINT32,     SOE_REQUIRED, 0 },
		{ sfInvalid, NULL, STI_DONE,		SOE_NEVER, -1 }
	}
};

void STObject::unitTest()
{
	STObject object1(testSOElements[0], "TestElement1");
	STObject object2(object1);
	if (object1.getSerializer() != object2.getSerializer()) throw std::runtime_error("STObject error");

	if (object1.isFieldPresent(sfTest2) || !object1.isFieldPresent(sfTest1))
		throw std::runtime_error("STObject error");

	object1.makeFieldPresent(sfTest2);
	if (!object1.isFieldPresent(sfTest2)) throw std::runtime_error("STObject Error");

	if ((object1.getFlags() != 1) || (object2.getFlags() != 0)) throw std::runtime_error("STObject error");
	if (object1.getFieldH256(sfTest2) != uint256()) throw std::runtime_error("STObject error");

	if (object1.getSerializer() == object2.getSerializer()) throw std::runtime_error("STObject error");
	object1.makeFieldAbsent(sfTest2);
	if (object1.isFieldPresent(sfTest2)) throw std::runtime_error("STObject error");
	if (object1.getFlags() != 0) throw std::runtime_error("STObject error");
	if (object1.getSerializer() != object2.getSerializer()) throw std::runtime_error("STObject error");

	STObject copy(object1);
	if (object1.isFieldPresent(sfTest2)) throw std::runtime_error("STObject error");
	if (copy.isFieldPresent(sfTest2)) throw std::runtime_error("STObject error");
	if (object1.getSerializer() != copy.getSerializer()) throw std::runtime_error("STObject error");
	copy.setFieldU32(sfTest3, 1);
	if (object1.getSerializer() == copy.getSerializer()) throw std::runtime_error("STObject error");
#ifdef DEBUG
	Log(lsDEBUG) << copy.getJson(0);
#endif

	for (int i = 0; i < 1000; i++)
	{
		std::cerr << "tol: i=" << i << std::endl;
		std::vector<unsigned char> j(i, 2);
		object1.setFieldVL(sfTest1, j);

		Serializer s;
		object1.add(s);
		SerializerIterator it(s);
		STObject object3(testSOElements[0], it, "TestElement3");

		if (object1.getFieldVL(sfTest1) != j) throw std::runtime_error("STObject error");
		if (object3.getFieldVL(sfTest1) != j) throw std::runtime_error("STObject error");
	}

}

#endif

// vim:ts=4
