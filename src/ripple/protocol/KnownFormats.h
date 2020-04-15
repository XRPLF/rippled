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

#ifndef RIPPLE_PROTOCOL_KNOWNFORMATS_H_INCLUDED
#define RIPPLE_PROTOCOL_KNOWNFORMATS_H_INCLUDED

#include <ripple/basics/contract.h>
#include <ripple/protocol/SOTemplate.h>
#include <boost/container/flat_map.hpp>
#include <algorithm>
#include <forward_list>

namespace ripple {

/** Manages a list of known formats.

    Each format has a name, an associated KeyType (typically an enumeration),
    and a predefined @ref SOElement.

    @tparam KeyType The type of key identifying the format.
*/
template <class KeyType>
class KnownFormats
{
public:
    /** A known format.
     */
    class Item
    {
    public:
        Item(
            char const* name,
            KeyType type,
            std::initializer_list<SOElement> uniqueFields,
            std::initializer_list<SOElement> commonFields)
            : soTemplate_(uniqueFields, commonFields), name_(name), type_(type)
        {
            // Verify that KeyType is appropriate.
            static_assert(
                std::is_enum<KeyType>::value ||
                    std::is_integral<KeyType>::value,
                "KnownFormats KeyType must be integral or enum.");
        }

        /** Retrieve the name of the format.
         */
        std::string const&
        getName() const
        {
            return name_;
        }

        /** Retrieve the transaction type this format represents.
         */
        KeyType
        getType() const
        {
            return type_;
        }

        SOTemplate const&
        getSOTemplate() const
        {
            return soTemplate_;
        }

    private:
        SOTemplate soTemplate_;
        std::string const name_;
        KeyType const type_;
    };

    /** Create the known formats object.

        Derived classes will load the object with all the known formats.
    */
    KnownFormats() = default;

    /** Destroy the known formats object.

        The defined formats are deleted.
    */
    virtual ~KnownFormats() = default;
    KnownFormats(KnownFormats const&) = delete;
    KnownFormats&
    operator=(KnownFormats const&) = delete;

    /** Retrieve the type for a format specified by name.

        If the format name is unknown, an exception is thrown.

        @param  name The name of the type.
        @return      The type.
    */
    KeyType
    findTypeByName(std::string const& name) const
    {
        Item const* const result = findByName(name);

        if (result != nullptr)
            return result->getType();
        Throw<std::runtime_error>("Unknown format name");
        return {};  // Silence compiler warning.
    }

    /** Retrieve a format based on its type.
     */
    Item const*
    findByType(KeyType type) const
    {
        auto const itr = types_.find(type);
        if (itr == types_.end())
            return nullptr;
        return itr->second;
    }

    // begin() and end() are provided for testing purposes.
    typename std::forward_list<Item>::const_iterator
    begin() const
    {
        return formats_.begin();
    }

    typename std::forward_list<Item>::const_iterator
    end() const
    {
        return formats_.end();
    }

protected:
    /** Retrieve a format based on its name.
     */
    Item const*
    findByName(std::string const& name) const
    {
        auto const itr = names_.find(name);
        if (itr == names_.end())
            return nullptr;
        return itr->second;
    }

    /** Add a new format.

        @param name The name of this format.
        @param type The type of this format.
        @param uniqueFields An std::initializer_list of unique fields
        @param commonFields An std::initializer_list of common fields

        @return The created format.
    */
    Item const&
    add(char const* name,
        KeyType type,
        std::initializer_list<SOElement> uniqueFields,
        std::initializer_list<SOElement> commonFields = {})
    {
        formats_.emplace_front(name, type, uniqueFields, commonFields);
        Item const& item{formats_.front()};

        names_[name] = &item;
        types_[type] = &item;

        return item;
    }

private:
    // One of the situations where a std::forward_list is useful.  We want to
    // store each Item in a place where its address won't change.  So a node-
    // based container is appropriate.  But we don't need searchability.
    std::forward_list<Item> formats_;

    boost::container::flat_map<std::string, Item const*> names_;
    boost::container::flat_map<KeyType, Item const*> types_;
};

}  // namespace ripple

#endif
