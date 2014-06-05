//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/common/jsonrpc_fields.h>

namespace ripple {

bool ParameterNode::setValue (const std::string& name, const Json::Value& value, Json::Value& error)
{
    if (name.empty ()) // this node
        return setValue (value, error);

    size_t dot = name.find ('.');

    if (dot == std::string::npos) // a child of this node
    {
        std::map<std::string, Parameter::pointer>::iterator it = mChildren.find (name);

        if (it == mChildren.end ())
        {
            error = Json::objectValue;
            error[jss::error] = "Name not found";
            error[jss::name] = name;
            return false;
        }

        return it->second->setValue (value, error);
    }

    std::map<std::string, Parameter::pointer>::iterator it = mChildren.find (name.substr (0, dot));

    if (it == mChildren.end ())
    {
        error = Json::objectValue;
        error[jss::error] = "Name not found";
        error[jss::name] = name;
        return false;
    }

    ParameterNode* n = dynamic_cast<ParameterNode*> (it->second.get ());

    if (!n)
    {
        error = Json::objectValue;
        error[jss::error] = "Node has no children";
        error[jss::name] = it->second->getName ();
        return false;
    }

    return n->setValue (name.substr (dot + 1), value, error);
}

bool ParameterNode::addNode (const std::string& name, Parameter::ref node)
{
    if (name.empty ()) // this node
        return false;

    size_t dot = name.find ('.');

    if (dot == std::string::npos) // a child of this node
    {
        std::map<std::string, Parameter::pointer>::iterator it = mChildren.find (name);

        if (it != mChildren.end ())
            return false;

        mChildren[name] = node;
        return true;
    }

    std::map<std::string, Parameter::pointer>::iterator it = mChildren.find (name.substr (0, dot));
    ParameterNode* n;

    if (it == mChildren.end ())
    {
        // create a new inner node
        ParameterNode::pointer node = std::make_shared<ParameterNode> (getShared (), name.substr (0, dot));
        n = dynamic_cast<ParameterNode*> (node.get ());
        assert (n);
        mChildren[name] = node;
    }
    else
    {
        // existing node passed through must be inner
        n = dynamic_cast<ParameterNode*> (it->second.get ());

        if (!n)
            return false;
    }

    return n->addNode (name.substr (dot + 1), node);
}

Json::Value ParameterNode::getValue (int i) const
{
    Json::Value v (Json::objectValue);
    typedef std::map<std::string, Parameter::pointer>::value_type string_ref_pair;
    BOOST_FOREACH (const string_ref_pair & it, mChildren)
    {
        v[it.first] = it.second->getValue (i);
    }
    return v;
}

bool ParameterNode::setValue (const Json::Value& value, Json::Value& error)
{
    error = Json::objectValue;
    error[jss::error] = "Cannot end on an inner node";

    Json::Value nodes (Json::arrayValue);
    typedef std::map<std::string, Parameter::pointer>::value_type string_ref_pair;
    BOOST_FOREACH (const string_ref_pair & it, mChildren)
    {
        nodes.append (it.first);
    }
    error["legal_nodes"] = nodes;
    return false;
}

ParameterString::ParameterString (Parameter::ref parent, const std::string& name, const std::string& value)
    : Parameter (parent, name), mValue (value)
{
    ;
}

Json::Value ParameterString::getValue (int) const
{
    return Json::Value (mValue);
}

bool ParameterString::setValue (const Json::Value& value, Json::Value& error)
{
    if (!value.isConvertibleTo (Json::stringValue))
    {
        error = Json::objectValue;
        error[jss::error] = "Cannot convert to string";
        error[jss::value] = value;
        return false;
    }

    mValue = value.asString ();
    return true;
}

ParameterInt::ParameterInt (Parameter::ref parent, const std::string& name, int value)
    : Parameter (parent, name), mValue (value)
{
    ;
}

Json::Value ParameterInt::getValue (int) const
{
    return Json::Value (mValue);
}

bool ParameterInt::setValue (const Json::Value& value, Json::Value& error)
{
    if (value.isConvertibleTo (Json::intValue))
    {
        mValue = value.asInt ();
        return true;
    }

    if (value.isConvertibleTo (Json::stringValue))
    {
        try
        {
            mValue = beast::lexicalCastThrow <int> (value.asString ());
        }
        catch (...)
        {
        }
    }

    error = Json::objectValue;
    error[jss::error] = "Cannot convert to integer";
    error[jss::value] = value;
    return false;
}

} // ripple
