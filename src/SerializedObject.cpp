
#include "SerializedObject.h"

#include "boost/lexical_cast.hpp"

#include "../json/writer.h"

SerializedType* STObject::makeDefaultObject(SerializedTypeID id, const char *name)
{
	switch(id)
	{
		case STI_UINT16:
			return new STUInt16(name);

		case STI_UINT32:
			return new STUInt32(name);

		case STI_UINT64:
			return new STUInt64(name);

		case STI_AMOUNT:
			return new STAmount(name);

		case STI_HASH160:
			return new STHash160(name);

		case STI_HASH256:
			return new STHash256(name);

		case STI_VL:
			return new STVariableLength(name);

		case STI_TL:
			return new STTaggedList(name);

		case STI_ACCOUNT:
			return new STAccount(name);

		default:
			assert(false);
			return NULL;
	}
}

SerializedType* STObject::makeDeserializedObject(SerializedTypeID id, const char *name, SerializerIterator& sit)
{
	switch(id)
	{
		case STI_UINT16:
			return STUInt16::construct(sit, name);

		case STI_UINT32:
			return STUInt32::construct(sit, name);

		case STI_UINT64:
			return STUInt64::construct(sit, name);

		case STI_AMOUNT:
			return STAmount::construct(sit, name);

		case STI_HASH160:
			return STHash160::construct(sit, name);

		case STI_HASH256:
			return STHash256::construct(sit, name);

		case STI_VL:
			return STVariableLength::construct(sit, name);

		case STI_TL:
			return STTaggedList::construct(sit, name);

		case STI_ACCOUNT:
			return STAccount::construct(sit, name);

		default:
			assert(false);
			return NULL;
	}
}

STObject::STObject(SOElement* elem, const char *name) : SerializedType(name), mFlagIdx(-1)
{
	while (elem->e_id != STI_DONE)
	{
		if (elem->e_type == SOE_FLAGS) mFlagIdx = mType.size();
		mType.push_back(elem);
		if (elem->e_type == SOE_IFFLAG)
			giveObject(new SerializedType(elem->e_name));
		else
		{
			SerializedType* t = makeDefaultObject(elem->e_id, elem->e_name);
			if (!t) throw std::runtime_error("invalid transaction element");
			giveObject(t);
		}
		++elem;
	}
}

STObject::STObject(SOElement* elem, SerializerIterator& sit, const char *name) : SerializedType(name), mFlagIdx(-1)
{
	int flags = -1;
	while (elem->e_id != STI_DONE)
	{
		mType.push_back(elem);
		bool done = false;
		if (elem->e_type == SOE_IFFLAG)
		{
			assert(flags >= 0);
			if ((flags&elem->e_flags) == 0) done = true;
		}
		else if (elem->e_type == SOE_IFNFLAG)
		{
			assert(flags >= 0);
			if ((flags&elem->e_flags) != 0) done = true;
		}
		else if (elem->e_type == SOE_FLAGS)
		{
			assert(elem->e_id == STI_UINT32);
			flags = sit.get32();
			mFlagIdx = giveObject(new STUInt32(elem->e_name, flags));
			done = true;
		}
		if (!done)
		{
			SerializedType* t = makeDeserializedObject(elem->e_id, elem->e_name, sit);
			if (!t) throw std::runtime_error("invalid transaction element");
			giveObject(t);
		}
		elem++;
	}
}

std::string STObject::getFullText() const
{
	std::string ret;
	bool first = true;
	if (name != NULL)
	{
		ret = name;
		ret += " = {";
	}
	else ret = "{";
	for (boost::ptr_vector<SerializedType>::const_iterator it = mData.begin(), end = mData.end(); it != end; ++it)
	{
		if (!first) ret += ", ";
		else first = false;
		ret += it->getFullText();
	}
	ret += "}";
	return ret;
}

int STObject::getLength() const
{
	int ret = 0;
	for (boost::ptr_vector<SerializedType>::const_iterator it = mData.begin(), end = mData.end(); it != end; ++it)
		ret += it->getLength();
	return ret;
}

void STObject::add(Serializer& s) const
{
	for (boost::ptr_vector<SerializedType>::const_iterator it = mData.begin(), end = mData.end(); it != end; ++it)
		it->add(s);
}

std::string STObject::getText() const
{
	std::string ret = "{";
	bool first = false;
	for (boost::ptr_vector<SerializedType>::const_iterator it = mData.begin(), end = mData.end(); it != end; ++it)
	{
		if (!first)
		{
			ret += ", ";
			first = false;
		}
		ret += it->getText();
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
		if (it1->getSType() != it2->getSType()) return false;
		if (!it1->isEquivalent(*it2)) return false;
		++it1;
		++it2;
	}
	return (it1 == end1) && (it2 == end2);
}

int STObject::getFieldIndex(SOE_Field field) const
{
	int i = 0;
	for (std::vector<SOElement*>::const_iterator it = mType.begin(), end = mType.end(); it != end; ++it, ++i)
		if ((*it)->e_field == field) return i;
	return -1;
}

const SerializedType& STObject::peekAtField(SOE_Field field) const
{
	int index = getFieldIndex(field);
	if (index == -1) throw std::runtime_error("Field not found");
	return peekAtIndex(index);
}

SerializedType& STObject::getField(SOE_Field field)
{
	int index = getFieldIndex(field);
	if (index==-1) throw std::runtime_error("Field not found");
	return getIndex(index);
}

const SerializedType* STObject::peekAtPField(SOE_Field field) const
{
	int index = getFieldIndex(field);
	if (index == -1) return NULL;
	return peekAtPIndex(index);
}

SerializedType* STObject::getPField(SOE_Field field)
{
	int index = getFieldIndex(field);
	if (index == -1) return NULL;
	return getPIndex(index);
}

bool STObject::isFieldPresent(SOE_Field field) const
{
	int index = getFieldIndex(field);
	if (index == -1) return false;
	return peekAtIndex(index).getSType() != STI_NOTPRESENT;
}

bool STObject::setFlag(uint32 f)
{
	if (mFlagIdx < 0) return false;
	STUInt32* t = dynamic_cast<STUInt32*>(getPIndex(mFlagIdx));
	assert(t);
	t->setValue(t->getValue() | f);
	return true;
}

bool STObject::clearFlag(uint32 f)
{
	if (mFlagIdx < 0) return false;
	STUInt32* t = dynamic_cast<STUInt32*>(getPIndex(mFlagIdx));
	assert(t);
	t->setValue(t->getValue() & ~f);
	return true;
}

uint32 STObject::getFlags(void) const
{
	if (mFlagIdx < 0) return 0;
	const STUInt32* t = dynamic_cast<const STUInt32*>(peekAtPIndex(mFlagIdx));
	assert(t);
	return t->getValue();
}

SerializedType* STObject::makeFieldPresent(SOE_Field field)
{
	SerializedType* ret = NULL;
	int index = getFieldIndex(field);
	if (index == -1) throw std::runtime_error("Field not found");
	if ((mType[index]->e_type != SOE_IFFLAG) && (mType[index]->e_type != SOE_IFNFLAG))
		throw std::runtime_error("field is not optional");

	ret = getPIndex(index);
	if (ret->getSType() != STI_NOTPRESENT) return ret;
	ret = makeDefaultObject(mType[index]->e_id, mType[index]->e_name);
	mData.replace(index, ret);

	if (mType[index]->e_type == SOE_IFFLAG)
		setFlag(mType[index]->e_flags);
	else if (mType[index]->e_type == SOE_IFNFLAG)
		clearFlag(mType[index]->e_flags);

	return ret;
}

void STObject::makeFieldAbsent(SOE_Field field)
{
	int index = getFieldIndex(field);
	if (index == -1) throw std::runtime_error("Field not found");
	if ((mType[index]->e_type != SOE_IFFLAG) && (mType[index]->e_type != SOE_IFNFLAG))
		throw std::runtime_error("field is not optional");

	if (peekAtIndex(index).getSType() == STI_NOTPRESENT) return;
	mData.replace(index, new SerializedType(mType[index]->e_name));

	if (mType[index]->e_type == SOE_IFFLAG)
		clearFlag(mType[index]->e_flags);
	else if (mType[index]->e_type == SOE_IFNFLAG)
		setFlag(mType[index]->e_flags);
}

std::string STObject::getFieldString(SOE_Field field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	return rf->getText();
}

unsigned char STObject::getValueFieldU8(SOE_Field field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt8 *cf = dynamic_cast<const STUInt8 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint16 STObject::getValueFieldU16(SOE_Field field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt16 *cf = dynamic_cast<const STUInt16 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint32 STObject::getValueFieldU32(SOE_Field field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt32 *cf = dynamic_cast<const STUInt32 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint64 STObject::getValueFieldU64(SOE_Field field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return 0; // optional field not present
	const STUInt64 *cf = dynamic_cast<const STUInt64 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint160 STObject::getValueFieldH160(SOE_Field field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return uint160(); // optional field not present
	const STHash160 *cf = dynamic_cast<const STHash160 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

uint256 STObject::getValueFieldH256(SOE_Field field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return uint256(); // optional field not present
	const STHash256 *cf = dynamic_cast<const STHash256 *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

NewcoinAddress STObject::getValueFieldAccount(SOE_Field field) const
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

std::vector<unsigned char> STObject::getValueFieldVL(SOE_Field field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return std::vector<unsigned char>(); // optional field not present
	const STVariableLength *cf = dynamic_cast<const STVariableLength *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

std::vector<TaggedListItem> STObject::getValueFieldTL(SOE_Field field) const
{
	const SerializedType* rf = peekAtPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	SerializedTypeID id = rf->getSType();
	if (id == STI_NOTPRESENT) return std::vector<TaggedListItem>(); // optional field not present
	const STTaggedList *cf = dynamic_cast<const STTaggedList *>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	return cf->getValue();
}

void STObject::setValueFieldU8(SOE_Field field, unsigned char v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt8* cf = dynamic_cast<STUInt8*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldU16(SOE_Field field, uint16 v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt16* cf = dynamic_cast<STUInt16*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldU32(SOE_Field field, uint32 v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt32* cf = dynamic_cast<STUInt32*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldU64(SOE_Field field, uint64 v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STUInt64* cf = dynamic_cast<STUInt64*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldH160(SOE_Field field, const uint160& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STHash160* cf = dynamic_cast<STHash160*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldH256(SOE_Field field, const uint256& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STHash256* cf = dynamic_cast<STHash256*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldAccount(SOE_Field field, const uint160& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STAccount* cf = dynamic_cast<STAccount*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValueH160(v);
}

void STObject::setValueFieldVL(SOE_Field field, const std::vector<unsigned char>& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STVariableLength* cf = dynamic_cast<STVariableLength*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

void STObject::setValueFieldTL(SOE_Field field, const std::vector<TaggedListItem>& v)
{
	SerializedType* rf = getPField(field);
	if (!rf) throw std::runtime_error("Field not found");
	if (rf->getSType() == STI_NOTPRESENT) rf = makeFieldPresent(field);
	STTaggedList* cf = dynamic_cast<STTaggedList*>(rf);
	if (!cf) throw std::runtime_error("Wrong field type");
	cf->setValue(v);
}

Json::Value STObject::getJson(int options) const
{
	Json::Value ret(Json::objectValue);
	int index = 1;
	for(boost::ptr_vector<SerializedType>::const_iterator it = mData.begin(), end = mData.end(); it != end;
		++it, ++index)
	{
		if (it->getSType() != STI_NOTPRESENT)
		{
			if (it->getName() == NULL)
				ret[boost::lexical_cast<std::string>(index)] = it->getText();
			else ret[it->getName()] = it->getText();
		}
	}
	return ret;
}

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
	Json::StyledStreamWriter ssw;
	ssw.write(std::cerr, copy.getJson(0));
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

// vim:ts=4
