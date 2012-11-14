
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
SField sfHash(STI_HASH256, 257, "hash");
SField sfIndex(STI_HASH256, 258, "index");

#define FIELD(name, type, index) SField sf##name(FIELD_CODE(STI_##type, index), STI_##type, index, #name);
#define TYPE(name, type, index)
#include "SerializeProto.h"
#undef FIELD
#undef TYPE

static int initFields()
{
	sfTxnSignature.notSigningField();		sfTxnSignatures.notSigningField();
	sfSignature.notSigningField();
	return 0;
}
static const int f = initFields();


SField::SField(SerializedTypeID tid, int fv) : fieldCode(FIELD_CODE(tid, fv)), fieldType(tid), fieldValue(fv)
{ // call with the map mutex
	fieldName = lexical_cast_i(tid) + "/" + lexical_cast_i(fv);
	codeToField[fieldCode] = this;
	assert((fv != 1) || ((tid != STI_ARRAY) && (tid!=STI_OBJECT)));
}

SField::ref SField::getField(int code)
{
	int type = code >> 16;
	int field = code % 0xffff;

	if ((type <= 0) || (field <= 0))
		return sfInvalid;

	boost::mutex::scoped_lock sl(mapMutex);

	std::map<int, SField::ptr>::iterator it = codeToField.find(code);
	if (it != codeToField.end())
		return *(it->second);

	if (field > 255)		// don't dynamically extend types that have no binary encoding
		return sfInvalid;

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

	return *(new SField(static_cast<SerializedTypeID>(type), field));
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
