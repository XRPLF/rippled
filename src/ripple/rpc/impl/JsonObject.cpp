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

#include <ripple/rpc/impl/JsonObject.h>
#include <ripple/rpc/impl/JsonWriter.h>

namespace ripple {
namespace RPC {
namespace New {

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

Collection& Collection::operator= (Collection&& that)
{
    parent_ = that.parent_;
    writer_ = that.writer_;
    enabled_ = that.enabled_;

    that.parent_ = nullptr;
    that.writer_ = nullptr;
    that.enabled_ = false;

    return *this;
}

Collection::Collection (Collection&& that)
{
    *this = std::move (that);
}

void Collection::checkWritable (std::string const& label)
{
    if (!enabled_)
        throw JsonException (label + ": not enabled");
    if (!writer_)
        throw JsonException (label + ": not writable");
}

//------------------------------------------------------------------------------

template <typename Scalar>
Array& Array::append (Scalar value)
{
    checkWritable ("append");
    if (writer_)
        writer_->append (value);
    return *this;
}

//------------------------------------------------------------------------------

Object::Root::Root (Writer& w) : Object (nullptr, &w)
{
    writer_->startRoot (Writer::object);
}

//------------------------------------------------------------------------------

template <typename Scalar>
Object& Object::set (std::string const& key, Scalar value)
{
    checkWritable ("set");
    if (writer_)
        writer_->set (key, value);
    return *this;
}

Object Object::makeObject (std::string const& key)
{
    checkWritable ("Object::makeObject");
    if (writer_)
        writer_->startSet (Writer::object, key);
    return Object (this, writer_);
}

Array Object::makeArray (std::string const& key) {
    checkWritable ("Object::makeArray");
    if (writer_)
        writer_->startSet (Writer::array, key);
    return Array (this, writer_);
}

Object Array::makeObject ()
{
    checkWritable ("Array::makeObject");
    if (writer_)
        writer_->startAppend (Writer::object);
    return Object (this, writer_);
}

Array Array::makeArray ()
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
    return {*this, key};
}

} // New
} // RPC
} // ripple
