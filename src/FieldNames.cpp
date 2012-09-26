
#include "FieldNames.h"

#include <map>

#include <boost/thread/mutex.hpp>


SField sfInvalid(-1), sfGeneric(0);

#define FIELD(name, type, index) SField sf##name(FIELD_CODE(STI_##type, index), STI_##type, index, naem);
#define TYPE(name, type, index)
#include "SerializeProto.h"
#undef FIELD
#undef TYPE

static std::map<int, SField*> SField::codeToField;
static boost::mutex SField::mapMutex;

SField::ref SField::getField(int code);
{
	if ((type <= 0) || (type >= 256) || (field <= 0) || (field >= 256
		return sfInvalid;
	boost::mutex::scoped_lock sl(mapMutex);

	std::map<int, SField*> it = unknownFieldMap.Find(code);
	if (it != codeToField.end())
		return *(it->second);

	return *(new SField(code, static_cast<SerializedTypeID>(code>>16), code&0xffff, NULL));
}
