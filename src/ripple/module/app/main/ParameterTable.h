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

#ifndef RIPPLE_PARAMETERTABLE_H
#define RIPPLE_PARAMETERTABLE_H

namespace ripple {

class Parameter : public std::enable_shared_from_this<Parameter>
{
    // abstract base class parameters are derived from
public:
    typedef std::shared_ptr<Parameter> pointer;
    typedef const std::shared_ptr<Parameter>& ref;

public:
    Parameter (Parameter::ref parent, const std::string& name) : mParent (parent), mName (name)
    {
        ;
    }
    virtual ~Parameter ()
    {
        ;
    }

    const std::string&  getName () const
    {
        return mName;
    }

    virtual Json::Value     getValue (int) const = 0;
    virtual bool            setValue (const Json::Value& value, Json::Value& error) = 0;

    Parameter::pointer      getShared ()
    {
        return shared_from_this ();
    }

private:
    pointer         mParent;
    std::string     mName;
};

class ParameterNode : public Parameter
{
public:
    ParameterNode (Parameter::ref parent, const std::string& name) : Parameter (parent, name)
    {
        ;
    }
    bool addChildNode (Parameter::ref node);

    bool setValue (const std::string& name, const Json::Value& value, Json::Value& error);
    bool addNode (const std::string& name, Parameter::ref node);

    virtual Json::Value     getValue (int) const;
    virtual bool            setValue (const Json::Value& value, Json::Value& error);

private:
    std::map<std::string, Parameter::pointer>   mChildren;
};

class ParameterString : public Parameter
{
public:
    ParameterString (Parameter::ref parent, const std::string& name, const std::string& value);
    virtual Json::Value     getValue (int) const;
    virtual bool            setValue (const Json::Value& value, Json::Value& error);

private:
    std::string     mValue;
};

class ParameterInt : public Parameter
{
public:
    ParameterInt (Parameter::ref parent, const std::string& name, int value);
    virtual Json::Value     getValue (int) const;
    virtual bool            setValue (const Json::Value& value, Json::Value& error);

private:
    int             mValue;
};

} // ripple

#endif
