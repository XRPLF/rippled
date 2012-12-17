#include "ParameterTable.h"

#include <boost/make_shared.hpp>
#include <boost/foreach.hpp>

#include "utils.h"

bool ParameterNode::setValue(const std::string& name, const Json::Value& value, Json::Value& error)
{
	if (name.empty()) // this node
		return setValue(value, error);

	size_t dot = name.find('.');
	if (dot == std::string::npos) // a child of this node
	{
		std::map<std::string, Parameter::pointer>::iterator it = mChildren.find(name);
		if (it == mChildren.end())
		{
			error = Json::objectValue;
			error["error"] = "Name not found";
			error["name"] = name;
			return false;
		}
		return it->second->setValue(value, error);
	}

	std::map<std::string, Parameter::pointer>::iterator it = mChildren.find(name.substr(0, dot));
	if (it == mChildren.end())
	{
		error = Json::objectValue;
		error["error"] = "Name not found";
		error["name"] = name;
		return false;
	}

	ParameterNode* n = dynamic_cast<ParameterNode*>(it->second.get());
	if (!n)
	{
		error = Json::objectValue;
		error["error"] = "Node has no children";
		error["name"] = it->second->getName();
	}

	return n->setValue(name.substr(dot + 1), value, error);
}

bool ParameterNode::addNode(const std::string& name, Parameter::ref node)
{
	if (name.empty()) // this node
		return false;

	size_t dot = name.find('.');
	if (dot == std::string::npos) // a child of this node
	{
		std::map<std::string, Parameter::pointer>::iterator it = mChildren.find(name);
		if (it != mChildren.end())
			return false;
		mChildren[name] = node;
		return true;
	}

	std::map<std::string, Parameter::pointer>::iterator it = mChildren.find(name.substr(0, dot));
	ParameterNode* n;
	if (it == mChildren.end())
	{ // create a new inner node
		ParameterNode::pointer node = boost::make_shared<ParameterNode>(getShared(), name.substr(0, dot));
		n = dynamic_cast<ParameterNode*>(node.get());
		assert(n);
		mChildren[name] = node;
	}
	else
	{ // existing node passed through must be inner
		ParameterNode* n = dynamic_cast<ParameterNode*>(it->second.get());
		if (!n)
			return false;
	}

	return n->addNode(name.substr(dot + 1), node);
}

Json::Value ParameterNode::getValue(int i) const
{
	Json::Value v(Json::objectValue);
	typedef std::pair<std::string, Parameter::ref> string_ref_pair;
	BOOST_FOREACH(const string_ref_pair& it, mChildren)
	{
		v[it.first] = it.second->getValue(i);
	}
	return v;
}

bool ParameterNode::setValue(const Json::Value& value, Json::Value& error)
{
	error = Json::objectValue;
	error["error"] = "Cannot end on an inner node";

	Json::Value nodes(Json::arrayValue);
	typedef std::pair<std::string, Parameter::ref> string_ref_pair;
	BOOST_FOREACH(const string_ref_pair& it, mChildren)
	{
		nodes.append(it.first);
	}
	error["legal_nodes"] = nodes;
	return false;
}

ParameterString::ParameterString(Parameter::ref parent, const std::string& name, const std::string& value)
	: Parameter(parent, name), mValue(value)
{ ; }

Json::Value ParameterString::getValue(int) const
{
	return Json::Value(mValue);
}

bool ParameterString::setValue(const Json::Value& value, Json::Value& error)
{
	if (!value.isConvertibleTo(Json::stringValue))
	{
		error = Json::objectValue;
		error["error"] = "Cannot convert to string";
		error["value"] = value;
		return false;
	}
	mValue = value.asString();
	return true;
}

ParameterInt::ParameterInt(Parameter::ref parent, const std::string& name, int value)
	: Parameter(parent, name), mValue(value)
{ ; }

Json::Value ParameterInt::getValue(int) const
{
	return Json::Value(mValue);
}

bool ParameterInt::setValue(const Json::Value& value, Json::Value& error)
{
	if (value.isConvertibleTo(Json::intValue))
	{
		mValue = value.asInt();
		return true;
	}
	if (value.isConvertibleTo(Json::stringValue))
	{
		try
		{
			mValue = lexical_cast_st<int>(value.asString());
		}
		catch (...)
		{
		}
	}
	error = Json::objectValue;
	error["error"] = "Cannot convert to integer";
	error["value"] = value;
	return false;
}
