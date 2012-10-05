
#include "FieldNames.h"

#include <map>

#include <boost/thread/mutex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>

#include "utils.h"

// These must stay at the top of this file
std::map<int, SField::ptr> SField::codeToField;
boost::mutex SField::mapMutex;

SField sfInvalid(-1), sfGeneric(0);
SField sfLedgerEntry(STI_LEDGERENTRY, 1, "LedgerEntry");
SField sfTransaction(STI_TRANSACTION, 1, "Transaction");
SField sfValidation(STI_VALIDATION, 1, "Validation");
SField sfID(STI_HASH256, 257, "id");

#define FIELD(name, type, index) SField sf##name(FIELD_CODE(STI_##type, index), STI_##type, index, #name);
#define TYPE(name, type, index)
#include "SerializeProto.h"
#undef FIELD
#undef TYPE


SField::ref SField::getField(int code)
{
	int type = code >> 16;
	int field = code % 0xffff;

	if ((type <= 0) || (field <= 0))
		return sfInvalid;
	{ //JED: Did this to fix a deadlock. david you should check. Line after this block also has a scoped lock
		// why doe sthis thing even need a mutex?
		boost::mutex::scoped_lock sl(mapMutex);

		std::map<int, SField::ptr>::iterator it = codeToField.find(code);
		if (it != codeToField.end())
			return *(it->second);

		switch (type)
		{ // types we are willing to dynamically extend

#define FIELD(name, type, index)
#define TYPE(name, type, index) case STI_##type:
#include "SerializeProto.h"
#undef FIELD
#undef TYPE

			break;
default:
	return sfInvalid;
		}

		std::string dynName = lexical_cast_i(type) + "/" + lexical_cast_i(field);
	}// end scope lock
	
	return *(new SField(code, static_cast<SerializedTypeID>(type), field, dynName.c_str()));
}

int SField::compare(SField::ref f1, SField::ref f2)
{ // -1 = f1 comes before f2, 0 = illegal combination, 1 = f1 comes after f2
	if ((f1.fieldCode <= 0) || (f2.fieldCode <= 0))
		return 0;

	if (f1.fieldCode < f2.fieldCode)
		return -1;

	if (f2.fieldCode < f1.fieldCode)
		return 1;

	return 0;
}

std::string SField::getName() const
{
	if (!fieldName.empty())
		return fieldName;
	if (fieldValue == 0)
		return "";
	return boost::lexical_cast<std::string>(static_cast<int>(fieldType)) + "/" +
		boost::lexical_cast<std::string>(fieldValue);
}

SField::ref SField::getField(const std::string& fieldName)
{ // OPTIMIZEME me with a map. CHECKME this is case sensitive
	boost::mutex::scoped_lock sl(mapMutex);
	typedef std::pair<const int, SField::ptr> int_sfref_pair;
	BOOST_FOREACH(const int_sfref_pair& fieldPair, codeToField)
	{
		if (fieldPair.second->fieldName == fieldName)
			return *(fieldPair.second);
	}
	return sfInvalid;
}

SField::~SField()
{
	boost::mutex::scoped_lock sl(mapMutex);
	std::map<int, ptr>::iterator it = codeToField.find(fieldCode);
	if ((it != codeToField.end()) && (it->second == this))
		codeToField.erase(it);
}

// vim:ts=4
