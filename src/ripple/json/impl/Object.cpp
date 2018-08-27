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

#include <ripple/basics/contract.h>
#include <ripple/json/Object.h>
#include <cassert>

namespace Json {

Collection::Collection (Collection* parent, Writer* writer)
        : parent_ (parent), writer_ (writer), enabled_ (true)
{
    checkWritable ("Collection::Collection()");
    if (parent_)
    {
        check (parent_->enabled_, "Parent not enabled in constructor");
        parent_->enabled_ = false;
    }
}

Collection::~Collection ()
{
    if (writer_)
        writer_->finish ();
    if (parent_)
        parent_->enabled_ = true;
}

Collection& Collection::operator= (Collection&& that) noexcept
{
    parent_ = that.parent_;
    writer_ = that.writer_;
    enabled_ = that.enabled_;

    that.parent_ = nullptr;
    that.writer_ = nullptr;
    that.enabled_ = false;

    return *this;
}

Collection::Collection (Collection&& that) noexcept
{
    *this = std::move (that);
}

void Collection::checkWritable (std::string const& label)
{
    if (! enabled_)
        ripple::Throw<std::logic_error> (label + ": not enabled");
    if (! writer_)
        ripple::Throw<std::logic_error> (label + ": not writable");
}

//------------------------------------------------------------------------------

Object::Root::Root (Writer& w) : Object (nullptr, &w)
{
    writer_->startRoot (Writer::object);
}

Object Object::setObject (std::string const& key)
{
    checkWritable ("Object::setObject");
    if (writer_)
        writer_->startSet (Writer::object, key);
    return Object (this, writer_);
}

Array Object::setArray (std::string const& key) {
    checkWritable ("Object::setArray");
    if (writer_)
        writer_->startSet (Writer::array, key);
    return Array (this, writer_);
}

//------------------------------------------------------------------------------

Object Array::appendObject ()
{
    checkWritable ("Array::appendObject");
    if (writer_)
        writer_->startAppend (Writer::object);
    return Object (this, writer_);
}

Array Array::appendArray ()
{
    checkWritable ("Array::makeArray");
    if (writer_)
        writer_->startAppend (Writer::array);
    return Array (this, writer_);
}

//------------------------------------------------------------------------------

Object::Proxy::Proxy (Object& object, std::string const& key)
    : object_ (object)
    , key_ (key)
{
}

Object::Proxy Object::operator[] (std::string const& key)
{
    return Proxy (*this, key);
}

Object::Proxy Object::operator[] (Json::StaticString const& key)
{
    return Proxy (*this, std::string (key));
}

//------------------------------------------------------------------------------

void Array::append (Json::Value const& v)
{
    auto t = v.type();
    switch (t)
    {
    case Json::nullValue:    return append (nullptr);
    case Json::intValue:     return append (v.asInt());
    case Json::uintValue:    return append (v.asUInt());
    case Json::realValue:    return append (v.asDouble());
    case Json::stringValue:  return append (v.asString());
    case Json::booleanValue: return append (v.asBool());

    case Json::objectValue:
    {
        auto object = appendObject ();
        copyFrom (object, v);
        return;
    }

    case Json::arrayValue:
    {
        auto array = appendArray ();
        for (auto& item: v)
            array.append (item);
        return;
    }
    }
    assert (false);  // Can't get here.
}

void Object::set (std::string const& k, Json::Value const& v)
{
    auto t = v.type();
    switch (t)
    {
    case Json::nullValue:    return set (k, nullptr);
    case Json::intValue:     return set (k, v.asInt());
    case Json::uintValue:    return set (k, v.asUInt());
    case Json::realValue:    return set (k, v.asDouble());
    case Json::stringValue:  return set (k, v.asString());
    case Json::booleanValue: return set (k, v.asBool());

    case Json::objectValue:
    {
        auto object = setObject (k);
        copyFrom (object, v);
        return;
    }

    case Json::arrayValue:
    {
        auto array = setArray (k);
        for (auto& item: v)
            array.append (item);
        return;
    }
    }
    assert (false);  // Can't get here.
}

//------------------------------------------------------------------------------

namespace {

template <class Object>
void doCopyFrom (Object& to, Json::Value const& from)
{
    assert (from.isObjectOrNull());
    auto members = from.getMemberNames();
    for (auto& m: members)
        to[m] = from[m];
}

}

void copyFrom (Json::Value& to, Json::Value const& from)
{
    if (!to)  // Short circuit this very common case.
        to = from;
    else
        doCopyFrom (to, from);
}

void copyFrom (Object& to, Json::Value const& from)
{
    doCopyFrom (to, from);
}

WriterObject stringWriterObject (std::string& s)
{
    return WriterObject (stringOutput (s));
}

} // Json
