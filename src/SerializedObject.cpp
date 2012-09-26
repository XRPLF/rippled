
#include "SerializedObject.h"

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include "../json/writer.h"

#include "Log.h"

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

void STObject::set(SOElement::ptr elem)
{
	mData.empty();
	mType.empty();

	while (elem->flags != SOE_END)
	{
		mType.push_back(elem);
		if (elem->flags == SOE_OPTIONAL)
			giveObject(makeNonPresentObject(elem->e_field));
		else
			giveObject(makeDefaultObject(elem->e_field));
		++elem;
	}
}

void STObject::setType(SOElement::ptrList t)
{
	mData.empty();
	while (t->flags != SOE_END)
		mType.push_back(t++);
}

bool STObject::isValidForType()
{
	BOOST_FOREACH(SOElement::ptr elem, mType)
	{ // are any required elemnents missing
		if ((elem->flags == SOE_REQUIRED) && (getPField(elem->e_field) == NULL))
		{
			Log(lsWARNING) << getName() << " missing required element " << elem->e_field.fieldName;
			return false;
		}
	}

	BOOST_FOREACH(const SerializedType& elem, mData)
	{ // are any non-permitted elements present
		if (!isFieldAllowed(elem.getFName()))
		{
			Log(lsWARNING) << getName() << " has non-permitted element " << elem.getName();
			return false;
		}
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

bool STObject::set(SOElement::ptrList elem, SerializerIterator& sit, int depth)
{ // return true = terminated with end-of-object
	setType(elem);

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

void STObject::add(Serializer& s) const
{
	addFieldID(s);
	addRaw(s);
	s.addFieldID(STI_OBJECT, 1);
}

void STObject::addRaw(Serializer& s) const
{ // FIXME: need to add in sorted order
	BOOST_FOREACH(const SerializedType& it, mData)
		it.add(s);
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
	if (!v) return false;
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

int STObject::getFieldIndex(SField::ref field) const
{
	int i = 0;
	for (std::vector<const SOElement*>::const_iterator it = mType.begin(), end = mType.end(); it != end; ++it, ++i)
		if ((*it)->e_field == field) return i;
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
	return mType[index]->e_field;
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
	mData.replace(index, makeDefaultObject(mType[index]->e_field));
	return getPIndex(index);
}

void STObject::makeFieldAbsent(SField::ref field)
{
	int index = getFieldIndex(field);
	if (index == -1)
		throw std::runtime_error("Field not found");
	if (mType[index]->flags != SOE_OPTIONAL)
		throw std::runtime_error("field is not optional");

	if (peekAtIndex(index).getSType() == STI_NOTPRESENT)
		return;

	mData.replace(index, makeDefaultObject(mType[index]->e_field));
}

std::string STObject::getFieldString(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	return rf->getText();
}

unsigned char STObject::getValueFieldU8(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt8 *cf = dynamic_cast<const STUInt8 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint16 STObject::getValueFieldU16(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt16 *cf = dynamic_cast<const STUInt16 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint32 STObject::getValueFieldU32(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt32 *cf = dynamic_cast<const STUInt32 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint64 STObject::getValueFieldU64(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt64 *cf = dynamic_cast<const STUInt64 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint128 STObject::getValueFieldH128(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return uint128(); // optional field not present
	const STHash128 *cf = dynamic_cast<const STHash128 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint160 STObject::getValueFieldH160(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return uint160(); // optional field not present
	const STHash160 *cf = dynamic_cast<const STHash160 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint256 STObject::getValueFieldH256(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return uint256(); // optional field not present
	const STHash256 *cf = dynamic_cast<const STHash256 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

NewcoinAddress STObject::getValueFieldAccount(SField::ref field) const
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

std::vector<unsigned char> STObject::getValueFieldVL(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return std::vector<unsigned char>(); // optional field not present
	const STVariableLength *cf = dynamic_cast<const STVariableLength *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

STAmount STObject::getValueFieldAmount(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return STAmount(); // optional field not present
	const STAmount *cf = dynamic_cast<const STAmount *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return *cf;
}

STPathSet STObject::getValueFieldPathSet(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return STPathSet(); // optional field not present
	const STPathSet *cf = dynamic_cast<const STPathSet *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return *cf;
}

STVector256 STObject::getValueFieldV256(SField::ref field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return STVector256(); // optional field not present
	const STVector256 *cf = dynamic_cast<const STVector256 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return *cf;
}

void STObject::setValueFieldU8(SField::ref field, unsigned char v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt8* cf = dynamic_cast<STUInt8*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldU16(SField::ref field, uint16 v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt16* cf = dynamic_cast<STUInt16*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldU32(SField::ref field, uint32 v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt32* cf = dynamic_cast<STUInt32*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldU64(SField::ref field, uint64 v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt64* cf = dynamic_cast<STUInt64*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldH128(SField::ref field, const uint128& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STHash128* cf = dynamic_cast<STHash128*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldH160(SField::ref field, const uint160& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STHash160* cf = dynamic_cast<STHash160*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldH256(SField::ref field, const uint256& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STHash256* cf = dynamic_cast<STHash256*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldV256(SField::ref field, const STVector256& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STVector256* cf = dynamic_cast<STVector256*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldAccount(SField::ref field, const uint160& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STAccount* cf = dynamic_cast<STAccount*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValueH160(v);
}

void STObject::setValueFieldVL(SField::ref field, const std::vector<unsigned char>& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STVariableLength* cf = dynamic_cast<STVariableLength*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldAmount(SField::ref field, const STAmount &v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STAmount* cf = dynamic_cast<STAmount*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	(*cf) = v;
}

void STObject::setValueFieldPathSet(SField::ref field, const STPathSet &v)
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
			if (it.getName() == NULL)
				ret[boost::lexical_cast<std::string>(index)] = it.getJson(options);
			else ret[it.getName()] = it.getJson(options);
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
	if (object1.getValueFieldH256(sfTest2) != uint256()) throw std::runtime_error("STObject error");

	if (object1.getSerializer() == object2.getSerializer()) throw std::runtime_error("STObject error");
	object1.makeFieldAbsent(sfTest2);
	if (object1.isFieldPresent(sfTest2)) throw std::runtime_error("STObject error");
	if (object1.getFlags() != 0) throw std::runtime_error("STObject error");
	if (object1.getSerializer() != object2.getSerializer()) throw std::runtime_error("STObject error");

	STObject copy(object1);
	if (object1.isFieldPresent(sfTest2)) throw std::runtime_error("STObject error");
	if (copy.isFieldPresent(sfTest2)) throw std::runtime_error("STObject error");
	if (object1.getSerializer() != copy.getSerializer()) throw std::runtime_error("STObject error");
	copy.setValueFieldU32(sfTest3, 1);
	if (object1.getSerializer() == copy.getSerializer()) throw std::runtime_error("STObject error");
#ifdef DEBUG
	Log(lsDEBUG) << copy.getJson(0);
#endif

	for (int i = 0; i < 1000; i++)
	{
		std::cerr << "tol: i=" << i << std::endl;
		std::vector<unsigned char> j(i, 2);
		object1.setValueFieldVL(sfTest1, j);

		Serializer s;
		object1.add(s);
		SerializerIterator it(s);
		STObject object3(testSOElements[0], it, "TestElement3");

		if (object1.getValueFieldVL(sfTest1) != j) throw std::runtime_error("STObject error");
		if (object3.getValueFieldVL(sfTest1) != j) throw std::runtime_error("STObject error");
	}

}

#endif

// vim:ts=4
