/**
 * @file KnownFormats.h
 * @brief Template base for XRPL protocol format registries.
 *
 * Declares `KnownFormats<KeyType, Derived>`, the shared infrastructure used
 * by `TxFormats`, `LedgerFormats`, and `InnerObjectFormats` to register and
 * look up the field schemas (`SOTemplate`) for every transaction type, ledger
 * object type, and inner object type in the protocol.
 */

#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/beast/type_name.h>
#include <xrpl/protocol/SOTemplate.h>

#include <boost/container/flat_map.hpp>

#include <algorithm>
#include <forward_list>

namespace xrpl {

/** Registry of protocol format schemas, keyed by a wire-protocol discriminant.
 *
 *  Each concrete registry (transaction, ledger entry, inner object) inherits
 *  from this template and populates it with one `Item` per recognized format.
 *  At runtime the serialization and validation layers look up `Item` instances
 *  via `findByType()` or `findTypeByName()` to obtain the `SOTemplate` that
 *  governs which fields are required, optional, or default for that format.
 *
 *  Concrete subclasses are singletons constructed during static initialization;
 *  registering the same `KeyType` value twice is a programming error caught via
 *  `logicError()` (process abort) at startup rather than at request time.
 *
 *  @note `begin()` / `end()` expose the raw `forward_list` for use in tests.
 *      They are not part of the normal lookup API.
 *
 *  @tparam KeyType  Integral or enum type whose values are the wire-protocol
 *      discriminants for this family of formats (e.g. `TxType`,
 *      `LedgerEntryType`, or `int` for inner objects).  The `static_assert`
 *      inside `Item`'s constructor enforces this constraint at compile time.
 *  @tparam Derived  The concrete subclass (CRTP).  Used solely to embed the
 *      subclass name in diagnostic messages via `beast::typeName<Derived>()`.
 *
 *  @see TxFormats, LedgerFormats, InnerObjectFormats, SOTemplate
 */
template <class KeyType, class Derived>
class KnownFormats
{
public:
    /** A registered protocol format: name, wire-key, and field schema.
     *
     *  Each `Item` bundles the human-readable format name (e.g. `"Payment"`),
     *  its `KeyType` discriminant (the integer embedded in the wire protocol),
     *  and the `SOTemplate` that specifies every field's presence requirement.
     *  `Item` instances are owned by `KnownFormats` and are never moved after
     *  construction; callers may hold `Item const*` pointers indefinitely.
     */
    class Item
    {
    public:
        /** Construct a format item and merge unique and common field lists.
         *
         *  `uniqueFields` are specific to this format; `commonFields` are
         *  shared across all formats in the registry (e.g. ledger metadata
         *  fields). Both lists are forwarded into the `SOTemplate`.
         *
         *  @note A `static_assert` enforces at compile time that `KeyType` is
         *      integral or an enum, preventing accidental use of arbitrary
         *      types as wire-protocol discriminants.
         *
         *  @param name          Human-readable format name (e.g. `"Payment"`).
         *  @param type          Wire-protocol discriminant value.
         *  @param uniqueFields  Fields specific to this format.
         *  @param commonFields  Fields shared by all formats in this registry.
         */
        Item(
            char const* name,
            KeyType type,
            std::vector<SOElement> uniqueFields,
            std::vector<SOElement> commonFields)
            : soTemplate_(std::move(uniqueFields), std::move(commonFields))
            , name_(name)
            , type_(type)
        {
            // KeyType must map directly to a wire integer value.
            static_assert(
                std::is_enum_v<KeyType> || std::is_integral_v<KeyType>,
                "KnownFormats KeyType must be integral or enum.");
        }

        /** Return the human-readable name of this format (e.g. `"Payment"`).
         */
        [[nodiscard]] std::string const&
        getName() const
        {
            return name_;
        }

        /** Return the wire-protocol discriminant identifying this format.
         *
         *  The returned value is the `KeyType` constant that was supplied at
         *  registration time — for example `ttPayment` for a transaction
         *  format, or `ltOFFER` for a ledger entry format.
         */
        [[nodiscard]] KeyType
        getType() const
        {
            return type_;
        }

        /** Return the field schema for this format.
         *
         *  The `SOTemplate` enumerates every field that may appear in a
         *  serialized object of this type, together with its `SOEStyle`
         *  (`soeREQUIRED`, `soeOPTIONAL`, or `soeDEFAULT`).  The returned
         *  reference is stable for the lifetime of the process.
         */
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

private:
    /** Construct the registry and capture the concrete subclass name.
     *
     *  The subclass name (obtained via `beast::typeName<Derived>()`) is
     *  stored in `name_` and prepended to diagnostic messages emitted by
     *  `findTypeByName()`, enabling errors like
     *  `"TxFormats: Unknown format name 'BadName'"`.
     *
     *  Only `Derived` may construct this base (enforced by the `friend`
     *  declaration and the private access specifier).
     */
    KnownFormats() : name_(beast::typeName<Derived>())
    {
    }

public:
    virtual ~KnownFormats() = default;
    KnownFormats(KnownFormats const&) = delete;
    KnownFormats&
    operator=(KnownFormats const&) = delete;

    /** Return the wire-protocol key for a format looked up by name.
     *
     *  Intended for use when parsing externally supplied strings (e.g. JSON
     *  RPC input or configuration files).  An unknown name is treated as a
     *  recoverable application-level error and is reported as a
     *  `std::runtime_error` whose message includes the registry name and the
     *  (possibly truncated to 32 characters) unrecognized name.
     *
     *  @param name  The human-readable format name to look up.
     *  @return      The `KeyType` value registered under that name.
     *  @throws std::runtime_error  If `name` is not registered in this
     *      registry.
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

    /** Return the `Item` registered for the given wire-protocol key, or
     *  `nullptr` if no such format has been registered.
     *
     *  Returns `nullptr` on a miss so that callers in internal code paths can
     *  handle an absent format with an idiomatic null check rather than a
     *  caught exception.  Contrast with `findTypeByName()`, which throws for
     *  unknown names supplied from external input.
     *
     *  @param type  The wire-protocol discriminant to look up.
     *  @return      Pointer to the matching `Item`, or `nullptr`.
     */
    [[nodiscard]] Item const*
    findByType(KeyType type) const
    {
        auto const itr = types_.find(type);
        if (itr == types_.end())
            return nullptr;
        return itr->second;
    }

    /** Return an iterator to the first registered `Item`.
     *
     *  @note Exposed for testing only; do not rely on iteration order, which
     *      reflects reverse-registration sequence due to `emplace_front`.
     */
    [[nodiscard]] typename std::forward_list<Item>::const_iterator
    begin() const
    {
        return formats_.begin();
    }

    /** Return a past-the-end iterator for the registered `Item` sequence.
     *
     *  @note Exposed for testing only.
     */
    [[nodiscard]] typename std::forward_list<Item>::const_iterator
    end() const
    {
        return formats_.end();
    }

protected:
    /** Return the `Item` registered under the given name, or `nullptr`.
     *
     *  Protected so that external callers are directed to the public
     *  `findTypeByName()`, which enforces the exception-on-miss contract for
     *  externally supplied names.
     *
     *  @param name  The human-readable format name to look up.
     *  @return      Pointer to the matching `Item`, or `nullptr`.
     */
    [[nodiscard]] Item const*
    findByName(std::string const& name) const
    {
        auto const itr = names_.find(name);
        if (itr == names_.end())
            return nullptr;
        return itr->second;
    }

    /** Register a new format with this registry.
     *
     *  Creates an `Item` by combining `uniqueFields` (specific to this
     *  format) and `commonFields` (shared across all formats in the registry)
     *  into a single `SOTemplate`.  The new `Item` is inserted at the front
     *  of the owning `forward_list` to preserve pointer stability, then
     *  indexed by both name and type in the two `flat_map` lookup tables.
     *
     *  Registering a `type` value that is already present is a programming
     *  error: `logicError()` (process abort) is called immediately, making
     *  the failure visible at static-initialization time before any requests
     *  are served.
     *
     *  @param name          Human-readable format name (e.g. `"Payment"`).
     *  @param type          Wire-protocol discriminant; must be unique within
     *      this registry.
     *  @param uniqueFields  Fields specific to this format.
     *  @param commonFields  Fields shared by all formats in this registry;
     *      defaults to empty.
     *  @return              A stable `const` reference to the newly created
     *      `Item`.
     */
    Item const&
    add(char const* name,
        KeyType type,
        std::vector<SOElement> uniqueFields,
        std::vector<SOElement> commonFields = {})
    {
        if (auto const item = findByType(type))
        {
            logicError(
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
    /** Concrete subclass name, captured at construction for diagnostic messages. */
    std::string name_;

    /** Owning store for all registered `Item` instances.
     *
     *  `std::forward_list` is used because node-based containers never
     *  relocate existing elements, keeping `Item` addresses stable after
     *  insertion.  The `flat_map` indices below store raw pointers into this
     *  list; pointer stability is therefore a hard requirement.
     */
    std::forward_list<Item> formats_{};

    /** Name-to-item index for O(log n) lookup by human-readable format name. */
    boost::container::flat_map<std::string, Item const*> names_{};

    /** Type-to-item index for O(log n) lookup by wire-protocol discriminant. */
    boost::container::flat_map<KeyType, Item const*> types_{};
    friend Derived;
};

}  // namespace xrpl
