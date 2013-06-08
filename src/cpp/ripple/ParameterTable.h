#ifndef RIPPLE_PARAMETERTABLE_H
#define RIPPLE_PARAMETERTABLE_H

class Parameter : public boost::enable_shared_from_this<Parameter>
{ // abstract base class parameters are derived from
public:
	typedef boost::shared_ptr<Parameter> pointer;
	typedef const boost::shared_ptr<Parameter>& ref;

public:
	Parameter(Parameter::ref parent, const std::string& name) : mParent(parent), mName(name)	{ ; }
	virtual ~Parameter()																		{ ; }

	const std::string&	getName() const	{ return mName; }

	virtual Json::Value		getValue(int) const = 0;
	virtual bool			setValue(const Json::Value& value, Json::Value& error) = 0;

	Parameter::pointer		getShared()	{ return shared_from_this(); }

private:
	pointer			mParent;
	std::string		mName;
};

class ParameterNode	: public Parameter
{
public:
	ParameterNode(Parameter::ref parent, const std::string& name) : Parameter(parent, name)		{ ; }
	bool addChildNode(Parameter::ref node);

	bool setValue(const std::string& name, const Json::Value& value, Json::Value& error);
	bool addNode(const std::string& name, Parameter::ref node);
	
	virtual Json::Value		getValue(int) const;
	virtual bool			setValue(const Json::Value& value, Json::Value& error);

private:
	std::map<std::string, Parameter::pointer>	mChildren;
};

class ParameterString : public Parameter
{
public:
	ParameterString(Parameter::ref parent, const std::string& name, const std::string& value);
	virtual Json::Value		getValue(int) const;
	virtual bool			setValue(const Json::Value& value, Json::Value& error);

private:
	std::string		mValue;
};

class ParameterInt : public Parameter
{
public:
	ParameterInt(Parameter::ref parent, const std::string& name, int value);
	virtual Json::Value		getValue(int) const;
	virtual bool			setValue(const Json::Value& value, Json::Value& error);

private:
	int				mValue;
};

#endif
