#ifndef PARAMETER_TABLE__H
#define PARAMETER_TABLE__H

#include <string>
#include <map>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "../json/value.h"

class Parameter : public boost::enable_shared_from_this<Parameter>
{ // abstract base class parameters are derived from
public:
	typedef boost::shared_ptr<Parameter> pointer;
	typedef const boost::shared_ptr<Parameter>& ref;

protected:
	pointer			mParent;
	std::string		mName;

public:
	Parameter(Parameter::ref parent, const std::string& name) : mParent(parent), mName(name)	{ ; }
	virtual ~Parameter()																		{ ; }

	const std::string&	getName() const	{ return mName; }

	virtual Json::Value		getValue(int) const = 0;
	virtual bool			setValue(const Json::Value& value, Json::Value& error) = 0;

	Parameter::pointer		getShared()	{ return shared_from_this(); }
};

class ParameterNode	: public Parameter
{
protected:
	std::map<std::string, Parameter::pointer>	mChildren;

public:
	ParameterNode(Parameter::ref parent, const std::string& name) : Parameter(parent, name)		{ ; }
	bool addChildNode(Parameter::ref node);

	bool setValue(const std::string& name, const Json::Value& value, Json::Value& error);
	bool addNode(const std::string& name, Parameter::ref node);
	
	virtual Json::Value		getValue(int) const;
	virtual bool			setValue(const Json::Value& value, Json::Value& error);
};

class ParameterString : public Parameter
{
protected:
	std::string		mValue;

public:
	ParameterString(Parameter::ref parent, const std::string& name, const std::string& value);
	virtual Json::Value		getValue(int) const;
	virtual bool			setValue(const Json::Value& value, Json::Value& error);
};

class ParameterInt : public Parameter
{
protected:
	int				mValue;

public:
	ParameterInt(Parameter::ref parent, const std::string& name, int value);
	virtual Json::Value		getValue(int) const;
	virtual bool			setValue(const Json::Value& value, Json::Value& error);
};

#endif
