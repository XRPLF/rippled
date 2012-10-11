
#include "SerializedObject.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/test/unit_test.hpp>

#include "../json/writer.h"

#include "Log.h"
#include "LedgerFormats.h"
#include "TransactionFormats.h"
#include "SerializedTransaction.h"

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
	mData.clear();
	mType.clear();

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

	mType.clear();
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
				Log(lsWARNING) << "setType !valid missing " << elem->e_field.fieldName;
				valid = false;
			}
			newData.push_back(makeNonPresentObject(elem->e_field));
		}

		mType.push_back(elem);
	}
	if (mData.size() != 0)
	{
		BOOST_FOREACH(const SerializedType& t, mData)
		{
			if (!t.getFName().isDiscardable())
			{
				Log(lsWARNING) << "setType !valid leftover: " << t.getFName().getName();
				valid = false;
			}
		}
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
	mData.clear();
	while (!sit.empty())
	{
		int type, field;
		sit.getFieldID(type, field);
		if ((type == STI_OBJECT) && (field == 1)) // end of object indicator
			return true;
		SField::ref fn = SField::getField(type, field);
		if (fn.isInvalid())
		{
			Log(lsWARNING) << "Unknown field: field_type=" << type << ", field_name=" << field;
			throw std::runtime_error("Unknown field");
		}
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
		if ((it.getSType() != STI_NOTPRESENT) && it.getFName().isBinary())
		{
			SField::ref fName = it.getFName();
			if (withSigningFields ||
					((fName != sfTxnSignature) && (fName != sfTxnSignatures) && (fName != sfSignature)))
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

SerializedType* STObject::getPField(SField::ref field, bool createOkay)
{
	int index = getFieldIndex(field);
	if (index == -1)
	{
		if (createOkay && isFree())
			return getPIndex(giveObject(makeDefaultObject(field)));
		return NULL;
	}
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
	STUInt32* t = dynamic_cast<STUInt32*>(getPField(sfFlags, true));
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
	{
		if (!isFree())
			throw std::runtime_error("Field not found");
		return getPIndex(giveObject(makeNonPresentObject(field)));
	}

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

	mData.replace(index, makeNonPresentObject(f.getFName()));
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
	SerializedType* rf = getPField(field, true);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt8* cf = dynamic_cast<STUInt8*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldU16(SField::ref field, uint16 v)
{
	SerializedType* rf = getPField(field, true);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt16* cf = dynamic_cast<STUInt16*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldU32(SField::ref field, uint32 v)
{
	SerializedType* rf = getPField(field, true);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt32* cf = dynamic_cast<STUInt32*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldU64(SField::ref field, uint64 v)
{
	SerializedType* rf = getPField(field, true);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt64* cf = dynamic_cast<STUInt64*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldH128(SField::ref field, const uint128& v)
{
	SerializedType* rf = getPField(field, true);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STHash128* cf = dynamic_cast<STHash128*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldH160(SField::ref field, const uint160& v)
{
	SerializedType* rf = getPField(field, true);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STHash160* cf = dynamic_cast<STHash160*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldH256(SField::ref field, const uint256& v)
{
	SerializedType* rf = getPField(field, true);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STHash256* cf = dynamic_cast<STHash256*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldV256(SField::ref field, const STVector256& v)
{
	SerializedType* rf = getPField(field, true);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STVector256* cf = dynamic_cast<STVector256*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldAccount(SField::ref field, const uint160& v)
{
	SerializedType* rf = getPField(field, true);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STAccount* cf = dynamic_cast<STAccount*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValueH160(v);
}

void STObject::setFieldVL(SField::ref field, const std::vector<unsigned char>& v)
{
	SerializedType* rf = getPField(field, true);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STVariableLength* cf = dynamic_cast<STVariableLength*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setFieldAmount(SField::ref field, const STAmount &v)
{
	SerializedType* rf = getPField(field, true);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STAmount* cf = dynamic_cast<STAmount*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	(*cf) = v;
}

void STObject::setFieldPathSet(SField::ref field, const STPathSet &v)
{
	SerializedType* rf = getPField(field, true);
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
	std::string r = "[";

	bool first = true;
	BOOST_FOREACH(const STObject& o, value)
	{
		if (!first)
			r += ",";
		r += o.getFullText();
		first = false;
	}

	r += "]";
	return r;
}

std::string STArray::getText() const
{
	std::string r = "[";

	bool first = true;
	BOOST_FOREACH(const STObject& o, value)
	{
		if (!first)
			r += ",";
		r += o.getText();
		first = false;
	}

	r += "]";
	return r;
}

Json::Value STArray::getJson(int p) const
{
	Json::Value v = Json::arrayValue;
	BOOST_FOREACH(const STObject& o, value)
		v.append(o.getJson(p));
	return v;
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
		{
			Log(lsTRACE) << "Unknown field: " << type << "/" << field;
			throw std::runtime_error("Unknown field");
		}

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
					data.push_back(new STUInt8(field, lexical_cast_st<unsigned char>(value.asString())));
				else if (value.isInt())	
				{
					if (value.asInt() < 0 || value.asInt() > 255)
						throw std::runtime_error("value out of rand");
					data.push_back(new STUInt8(field, range_check_cast<unsigned char>(value.asInt(), 0, 255)));
				}
				else if (value.isUInt())
				{
					if (value.asUInt() > 255)
						throw std::runtime_error("value out of rand");
					data.push_back(new STUInt8(field, range_check_cast<unsigned char>(value.asUInt(), 0, 255)));
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
						data.push_back(new STUInt16(field, lexical_cast_st<uint16>(strValue)));
				}
				else if (value.isInt())
					data.push_back(new STUInt16(field, range_check_cast<uint16>(value.asInt(), 0, 65535)));
				else if (value.isUInt())
					data.push_back(new STUInt16(field, range_check_cast<uint16>(value.asUInt(), 0, 65535)));
				else
					throw std::runtime_error("Incorrect type");
				break;

			case STI_UINT32:
				if (value.isString())
					data.push_back(new STUInt32(field, lexical_cast_st<uint32>(value.asString())));
				else if (value.isInt())
					data.push_back(new STUInt32(field, range_check_cast<uint32>(value.asInt(), 0, 4294967295)));
				else if (value.isUInt())
					data.push_back(new STUInt32(field, static_cast<uint32>(value.asUInt())));
				else
					throw std::runtime_error("Incorrect type");
				break;

			case STI_UINT64:
				if (value.isString())
					data.push_back(new STUInt64(field, uintFromHex(value.asString())));
				else if (value.isInt())
					data.push_back(new STUInt64(field,
						range_check_cast<uint64>(value.asInt(), 0, 18446744073709551615ull)));
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
				if (!value.isString())
					throw std::runtime_error("Incorrect type");
				data.push_back(new STVariableLength(field, strUnHex(value.asString())));
				break;

			case STI_AMOUNT:
				data.push_back(new STAmount(field, value));
				break;

			case STI_VECTOR256:
				if (!value.isArray())
					throw std::runtime_error("Incorrect type");
				{
					data.push_back(new STVector256(field));
					STVector256* tail = dynamic_cast<STVector256*>(&data.back());
					assert(tail);
					for (Json::UInt i = 0; !object.isValidIndex(i); ++i)
					{
						uint256 s;
						s.SetHex(object[i].asString());
						tail->addValue(s);
					}
				}
				break;

			case STI_PATHSET:
				if (!value.isArray())
					throw std::runtime_error("Path set must be array");
				{
					data.push_back(new STPathSet(field));
					STPathSet* tail = dynamic_cast<STPathSet*>(&data.back());
					assert(tail);
					for (Json::UInt i = 0; !object.isValidIndex(i); ++i)
					{
						STPath p;
						if (!object[i].isArray())
							throw std::runtime_error("Path must be array");
						for (Json::UInt j = 0; !object[i].isValidIndex(j); ++j)
						{ // each element in this path has some combination of account, currency, or issuer

							Json::Value pathEl = object[i][j];
							if (!pathEl.isObject())
								throw std::runtime_error("Path elements must be objects");
							const Json::Value& account =	pathEl["account"];
							const Json::Value& currency =	pathEl["currency"];
							const Json::Value& issuer =		pathEl["issuer"];

							uint160 uAccount, uCurrency, uIssuer;
							bool hasCurrency;
							if (!account.isNull())
							{ // human account id
								if (!account.isString())
									throw std::runtime_error("path element accounts must be strings");
								std::string strValue = account.asString();
								if (value.size() == 40) // 160-bit hex account value
									uAccount.SetHex(strValue);
								{
									NewcoinAddress a;
									if (!a.setAccountPublic(strValue))
										throw std::runtime_error("Account in path element invalid");
									uAccount = a.getAccountID();
								}
							}
							if (!currency.isNull())
							{ // human currency
								if (!currency.isString())
									throw std::runtime_error("path element currencies must be strings");
								hasCurrency = true;
								if (currency.asString().size() == 40)
									uCurrency.SetHex(currency.asString());
								else if (!STAmount::currencyFromString(uCurrency, currency.asString()))
									throw std::runtime_error("invalid currency");
							}
							if (!issuer.isNull())
							{ // human account id
								if (!issuer.isString())
									throw std::runtime_error("path element issuers must be strings");
								if (issuer.asString().size() == 40)
									uIssuer.SetHex(issuer.asString());
								else
								{
									NewcoinAddress a;
									if (!a.setAccountPublic(issuer.asString()))
										throw std::runtime_error("path element issuer invalid");
									uIssuer = a.getAccountID();
								}
							}
							p.addElement(STPathElement(uAccount, uCurrency, uIssuer, hasCurrency));
						}
						tail->addPath(p);
					}
				}
				break;

			case STI_ACCOUNT:
			{
				if (!value.isString())
					throw std::runtime_error("Incorrect type");
				std::string strValue = value.asString();
				if (value.size() == 40) // 160-bit hex account value
				{
					uint160 v;
					v.SetHex(strValue);
					data.push_back(new STAccount(field, v));
				}
				else
				{ // newcoin addres
					NewcoinAddress a;
					if (!a.setAccountID(strValue))
					{
						Log(lsINFO) << "Invalid acccount JSON: " << fieldName << ": " << strValue;
						throw std::runtime_error("Account invalid");
					}
					data.push_back(new STAccount(field, a.getAccountID()));
				}
			}
			break;

			case STI_OBJECT:
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
				if (!value.isArray())
					throw std::runtime_error("Inner value is not an array");
				{
					data.push_back(new STArray(field));
					STArray* tail = dynamic_cast<STArray*>(&data.back());
					assert(tail);
					for (Json::UInt i = 0; !object.isValidIndex(i); ++i)
						tail->push_back(*STObject::parseJson(object[i], sfGeneric, depth + 1));
				}

			default:
				throw std::runtime_error("Invalid field type");
		}
	}

	return std::auto_ptr<STObject>(new STObject(*name, data));
}

BOOST_AUTO_TEST_SUITE(SerializedObject)

BOOST_AUTO_TEST_CASE( FieldManipulation_test )
{
	SField sfTestVL(STI_VL, 255, "TestVL");
	SField sfTestH256(STI_HASH256, 255, "TestH256");
	SField sfTestU32(STI_UINT32, 255, "TestU32");
	SField sfTestObject(STI_OBJECT, 255, "TestObject");

	std::vector<SOElement::ptr> elements;
	elements.push_back(new SOElement(sfFlags, SOE_REQUIRED));
	elements.push_back(new SOElement(sfTestVL, SOE_REQUIRED));
	elements.push_back(new SOElement(sfTestH256, SOE_OPTIONAL));
	elements.push_back(new SOElement(sfTestU32, SOE_REQUIRED));

	STObject object1(elements, sfTestObject);
	STObject object2(object1);
	if (object1.getSerializer() != object2.getSerializer()) BOOST_FAIL("STObject error 1");

	if (object1.isFieldPresent(sfTestH256) || !object1.isFieldPresent(sfTestVL))
		BOOST_FAIL("STObject error");

	object1.makeFieldPresent(sfTestH256);
	if (!object1.isFieldPresent(sfTestH256)) BOOST_FAIL("STObject Error 2");
	if (object1.getFieldH256(sfTestH256) != uint256()) BOOST_FAIL("STObject error 3");

	if (object1.getSerializer() == object2.getSerializer())
	{
		Log(lsINFO) << "O1: " << object1.getJson(0);
		Log(lsINFO) << "O2: " << object2.getJson(0);
		BOOST_FAIL("STObject error 4");
	}
	object1.makeFieldAbsent(sfTestH256);
	if (object1.isFieldPresent(sfTestH256)) BOOST_FAIL("STObject error 5");
	if (object1.getFlags() != 0) BOOST_FAIL("STObject error 6");
	if (object1.getSerializer() != object2.getSerializer()) BOOST_FAIL("STObject error 7");

	STObject copy(object1);
	if (object1.isFieldPresent(sfTestH256)) BOOST_FAIL("STObject error 8");
	if (copy.isFieldPresent(sfTestH256)) BOOST_FAIL("STObject error 9");
	if (object1.getSerializer() != copy.getSerializer()) BOOST_FAIL("STObject error 10");
	copy.setFieldU32(sfTestU32, 1);
	if (object1.getSerializer() == copy.getSerializer()) BOOST_FAIL("STObject error 11");

	for (int i = 0; i < 1000; i++)
	{
		std::vector<unsigned char> j(i, 2);

		object1.setFieldVL(sfTestVL, j);

		Serializer s;
		object1.add(s);
		SerializerIterator it(s);

		STObject object3(elements, it, sfTestObject);

		if (object1.getFieldVL(sfTestVL) != j) BOOST_FAIL("STObject error");
		if (object3.getFieldVL(sfTestVL) != j) BOOST_FAIL("STObject error");
	}

}

BOOST_AUTO_TEST_SUITE_END();

// vim:ts=4
