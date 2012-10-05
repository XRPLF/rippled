#ifndef __FIELDNAMES__
#define __FIELDNAMES__

#include <string>

#include <boost/thread/mutex.hpp>

#define FIELD_CODE(type, index) ((static_cast<int>(type) << 16) | index)

enum SerializedTypeID
{
	// special types
	STI_UNKNOWN		= -2,
	STI_DONE		= -1,
	STI_NOTPRESENT	= 0,

#define TYPE(name, field, value) STI_##field = value,
#define FIELD(name, field, value)
#include "SerializeProto.h"
#undef TYPE
#undef FIELD

	// high level types
	STI_TRANSACTION = 10001,
	STI_LEDGERENTRY	= 10002,
	STI_VALIDATION	= 10003,
};

enum SOE_Flags
{
	SOE_INVALID  = -1,
	SOE_REQUIRED = 0,	// required
	SOE_OPTIONAL = 1,	// optional
};

class SField
{
public:
	typedef const SField&	ref;
	typedef SField const *	ptr;

protected:
	static std::map<int, ptr>	codeToField;
	static boost::mutex			mapMutex;

public:

	const int				fieldCode;		// (type<<16)|index
	const SerializedTypeID	fieldType;		// STI_*
	const int				fieldValue;		// Code number for protocol
	std::string				fieldName;

	SField(int fc, SerializedTypeID tid, int fv, const char* fn) : 
		fieldCode(fc), fieldType(tid), fieldValue(fv), fieldName(fn)
	{
		boost::mutex::scoped_lock sl(mapMutex);
		codeToField[fieldCode] = this;
	}

	SField(SerializedTypeID tid, int fv, const char *fn) :
		fieldCode(FIELD_CODE(tid, fv)), fieldType(tid), fieldValue(fv), fieldName(fn)
	{
		boost::mutex::scoped_lock sl(mapMutex);
		codeToField[fieldCode] = this;
	}

	SField(int fc) : fieldCode(fc), fieldType(STI_UNKNOWN), fieldValue(0) { ; }

	~SField();

	static SField::ref getField(int fieldCode);
	static SField::ref getField(const std::string& fieldName);
	static SField::ref getField(int type, int value)				{ return getField(FIELD_CODE(type, value)); }
	static SField::ref getField(SerializedTypeID type, int value)	{ return getField(FIELD_CODE(type, value)); }

	std::string getName() const;
	bool hasName() const		{ return !fieldName.empty(); }

	bool isGeneric() const		{ return fieldCode == 0; }
	bool isInvalid() const		{ return fieldCode == -1; }
	bool isKnown() const		{ return fieldType != STI_UNKNOWN; }
	bool isBinary() const		{ return fieldValue < 256; }

	bool operator==(const SField& f) const { return fieldCode == f.fieldCode; }
	bool operator!=(const SField& f) const { return fieldCode != f.fieldCode; }

	static int compare(SField::ref f1, SField::ref f2);
};

extern SField sfInvalid, sfGeneric, sfLedgerEntry, sfTransaction, sfValidation;

#define FIELD(name, type, index) extern SField sf##name;
#define TYPE(name, type, index)
#include "SerializeProto.h"
#undef FIELD
#undef TYPE

#endif
