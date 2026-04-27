#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/beast/type_name.h>
#include <xrpl/protocol/SOTemplate.h>

#include <boost/container/flat_map.hpp>

#include <algorithm>
#include <forward_list>

namespace xrpl {

/** Manages a list of known formats.

    Each format has a name, an associated KeyType (typically an enumeration),
    and a predefined @ref SOElement.

    @tparam KeyType The type of key identifying the format.
*/
template <class KeyType, class Derived>
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
            std::vector<SOElement> uniqueFields,
            std::vector<SOElement> commonFields)
            : soTemplate_(std::move(uniqueFields), std::move(commonFields))
            , name_(name)
            , type_(type)
        {
            // Verify that KeyType is appropriate.
            static_assert(
                std::is_enum_v<KeyType> || std::is_integral_v<KeyType>,
                "KnownFormats KeyType must be integral or enum.");
        }

        /** Retrieve the name of the format.
         */
        [[nodiscard]] std::string const&
        getName() const
        {
            return name_;
        }

        /** Retrieve the transaction type this format represents.
         */
        [[nodiscard]] KeyType
        getType() const
        {
            return type_;
        }

        [[nodiscard]] SOTemplate const&
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
private:
    KnownFormats() : name_(beast::type_name<Derived>())
    {
    }

public:
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
    [[nodiscard]] KeyType
    findTypeByName(std::string const& name) const
    {
        if (auto const result = findByName(name))
            return result->getType();
        Throw<std::runtime_error>(
            name_ + ": Unknown format name '" +
            name.substr(0, std::min(name.size(), std::size_t(32))) + "'");
    }

    /** Retrieve a format based on its type.
     */
    [[nodiscard]] Item const*
    findByType(KeyType type) const
    {
        auto const itr = types_.find(type);
        if (itr == types_.end())
            return nullptr;
        return itr->second;
    }

    // begin() and end() are provided for testing purposes.
    [[nodiscard]] typename std::forward_list<Item>::const_iterator
    begin() const
    {
        return formats_.begin();
    }

    [[nodiscard]] typename std::forward_list<Item>::const_iterator
    end() const
    {
        return formats_.end();
    }

protected:
    /** Retrieve a format based on its name.
     */
    [[nodiscard]] Item const*
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
        @param uniqueFields A std::vector of unique fields
        @param commonFields A std::vector of common fields

        @return The created format.
    */
    Item const&
    add(char const* name,
        KeyType type,
        std::vector<SOElement> uniqueFields,
        std::vector<SOElement> commonFields = {})
    {
        if (auto const item = findByType(type))
        {
            LogicError(
                std::string("Duplicate key for item '") + name + "': already maps to " +
                item->getName());
        }

        formats_.emplace_front(name, type, std::move(uniqueFields), std::move(commonFields));
        Item const& item{formats_.front()};

        names_[name] = &item;
        types_[type] = &item;

        return item;
    }

private:
    std::string name_;

    // One of the situations where a std::forward_list is useful.  We want to
    // store each Item in a place where its address won't change.  So a node-
    // based container is appropriate.  But we don't need searchability.
    std::forward_list<Item> formats_{};

    boost::container::flat_map<std::string, Item const*> names_{};
    boost::container::flat_map<KeyType, Item const*> types_{};
    friend Derived;
};

}  // namespace xrpl
