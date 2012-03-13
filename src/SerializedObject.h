#ifndef __SERIALIZEDOBJECT__
#define __SERIALIZEDOBJECT__

#include "SerializedTypes.h"

struct SOElement
{ // An element in the description of a serialized object
	const char *e_name;
	int e_flag;
	SerializedTypeID e_id;

	SOElement(const char *n, int f, SerializedTypeID i) : e_name(n), e_flag(f), e_id(i) { ; }
};

struct SOType
{ // A type of serialized object
	const char *name;
	std::list<SOElement> elements;
};

class STUObject : public SerializedType
{
protected:
	SOType *type;
	std::list<SerializedType*> data;

public:
	STUObject() { ; }
	virtual ~STUObject();

	STUObject(const STUObject&);
	STUObject& operator=(const STUObject&);

	int getLength() const;
	SerializedTypeID getType() const { return STI_OBJECT; }
	STUObject* duplicate() const { return new STUObject(*this); }

	std::vector<unsigned char> serialize() const;
	std::string getText() const;
	std::string getSQL() const;

	void addObject(const SerializedType& t) { data.push_back(t.duplicate()); }
	void giveObject(SerializedType* t) { data.push_back(t); }
	const std::list<SerializedType*>& peekData() const { return data; }
	std::list<SerializedType*>& peekData() { return data; }
};


#endif
