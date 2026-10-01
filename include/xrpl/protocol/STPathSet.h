#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/enum_bitops.h>
#include <xrpl/beast/hash/hash_append.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

enum class STPathElementType : std::uint8_t {
    // clang-format off

    None     = 0x00,  ///< No element; also terminates the serialized path set.
    Account  = 0x01,  ///< Rippling through an account (vs taking an offer).
    Currency = 0x10,  ///< Currency follows.
    Issuer   = 0x20,  ///< Issuer follows.
    Mpt      = 0x40,  ///< MPT follows.
    Boundary = 0xFF,  ///< Boundary between alternate paths.

    Asset    = Currency | Mpt,                      ///< Either asset kind.
    All      = Account | Currency | Issuer | Mpt,   ///< Every value-carrying bit.

    // clang-format on
};

template <>
struct enum_bitops::OptIn<STPathElementType> : std::true_type
{
};

class STPathElement final : public CountedObject<STPathElement>
{
    STPathElementType type_ = STPathElementType::None;
    bool isOffer_ = true;
    AccountID accountID_;
    PathAsset assetID_;
    AccountID issuerID_;

public:
    STPathElement() = default;
    STPathElement(STPathElement const&) = default;
    STPathElement&
    operator=(STPathElement const&) = default;

    STPathElement(
        std::optional<AccountID> const& account,
        std::optional<PathAsset> const& asset,
        std::optional<AccountID> const& issuer);

    STPathElement(
        AccountID const& account,
        PathAsset const& asset,
        AccountID const& issuer,
        bool forceAsset = false);

    STPathElement(
        STPathElementType type,
        AccountID const& account,
        PathAsset const& asset,
        AccountID const& issuer);

    [[nodiscard]] STPathElementType
    getNodeType() const noexcept;

    [[nodiscard]] bool
    isOffer() const noexcept;

    [[nodiscard]] bool
    isAccount() const noexcept;

    [[nodiscard]] bool
    hasIssuer() const noexcept;

    [[nodiscard]] bool
    hasCurrency() const noexcept;

    [[nodiscard]] bool
    hasMPT() const noexcept;

    [[nodiscard]] bool
    hasAsset() const noexcept;

    [[nodiscard]] bool
    isNone() const noexcept;

    // Nodes are either an account ID or a offer prefix. Offer prefixs denote a
    // class of offers.
    [[nodiscard]] AccountID const&
    getAccountID() const noexcept;

    [[nodiscard]] PathAsset const&
    getPathAsset() const noexcept;

    [[nodiscard]] Currency const&
    getCurrency() const;

    [[nodiscard]] MPTID const&
    getMPTID() const;

    [[nodiscard]] AccountID const&
    getIssuerID() const noexcept;

    [[nodiscard]] bool
    isType(STPathElementType pe) const noexcept;

    friend bool
    operator==(STPathElement const& lhs, STPathElement const& rhs) noexcept
    {
        return lhs.isType(STPathElementType::Account) == rhs.isType(STPathElementType::Account) &&
            lhs.accountID_ == rhs.accountID_ && lhs.assetID_ == rhs.assetID_ &&
            lhs.issuerID_ == rhs.issuerID_;
    }
};

template <class Hasher>
void
hash_append(Hasher& h, STPathElement const& e) noexcept
{
    using beast::hash_append;
    hash_append(h, e.isType(STPathElementType::Account));
    hash_append(h, e.getAccountID());
    hash_append(h, e.getPathAsset());
    hash_append(h, e.getIssuerID());
}

class STPath final : public CountedObject<STPath>
{
    std::vector<STPathElement> path_;

public:
    STPath() = default;

    STPath(std::vector<STPathElement> p);

    [[nodiscard]] std::vector<STPathElement>::size_type
    size() const;

    [[nodiscard]] bool
    empty() const;

    void
    pushBack(STPathElement const& e);

    template <typename... Args>
    void
    emplaceBack(Args&&... args);

    [[nodiscard]] bool
    hasSeen(AccountID const& account, PathAsset const& asset, AccountID const& issuer) const;

    [[nodiscard]] json::Value getJson(JsonOptions) const;

    [[nodiscard]] std::vector<STPathElement>::const_iterator
    begin() const;

    [[nodiscard]] std::vector<STPathElement>::const_iterator
    end() const;

    bool
    operator==(STPath const& t) const;

    [[nodiscard]] std::vector<STPathElement>::const_reference
    back() const;

    [[nodiscard]] std::vector<STPathElement>::const_reference
    front() const;

    STPathElement&
    operator[](int i);

    STPathElement const&
    operator[](int i) const;

    void
    reserve(size_t s);
};

template <class Hasher>
void
hash_append(Hasher& h, STPath const& p) noexcept
{
    using beast::hash_append;
    for (auto const& e : p)
    {
        hash_append(h, e);
    }
}

//------------------------------------------------------------------------------

// A set of zero or more payment paths
class STPathSet final : public STBase, public CountedObject<STPathSet>
{
    std::vector<STPath> value_;

    /**
     * Deduplication index over `value_`, for pathfinding.
     * The use of a std::unique_ptr is intentional as it
     * only requires 8 additional bytes of storage for the pointer
     * as opposed to 64 bytes with an optional.  This keeps the size
     * of the STPathSet to within the `STVar::kMaxSize` limit of 72 bytes.
     */
    std::unique_ptr<HardenedHashSet<STPath>> seen_;

public:
    struct DeduplicationTag
    {
    };

    STPathSet() = default;

    /**
     * Deduplication tagged constructor.
     * Use when you want to ensure that the STPathSet does not contain duplicate paths.
     */
    explicit STPathSet(DeduplicationTag);

    STPathSet(SField const& n);
    STPathSet(SerialIter& sit, SField const& name);
    STPathSet(STPathSet const& other);
    STPathSet(STPathSet&&) = default;

    STPathSet&
    operator=(STPathSet const& other);
    STPathSet&
    operator=(STPathSet&&) = default;

    ~STPathSet() override = default;

    void
    add(Serializer& s) const override;

    [[nodiscard]] json::Value getJson(JsonOptions) const override;

    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /**
     * @brief assembleAdd adds a path to the set by combining a base path and a tail element.
     *
     * @param base The base path.
     * @param tail The tail element.
     * @return true if the path was added, false if it was a duplicate and not added.
     * @remarks Requires the STPathSet to be constructed with the DeduplicationTag. The return value
     * indicates whether the combined path was inserted (true) or rejected as a duplicate (false).
     * It is fine for callers to ignore the return value.
     */
    bool
    assembleAdd(STPath const& base, STPathElement const& tail);

    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    [[nodiscard]] bool
    isDefault() const override;

    // std::vector like interface:
    std::vector<STPath>::const_reference
    operator[](std::vector<STPath>::size_type n) const;

    [[nodiscard]] std::vector<STPath>::const_iterator
    begin() const;

    [[nodiscard]] std::vector<STPath>::const_iterator
    end() const;

    [[nodiscard]] std::vector<STPath>::size_type
    size() const;

    [[nodiscard]] bool
    empty() const;

    /**
     * @brief pushBack adds a path to the set.
     *
     * @param e The path to add.
     * @return true if the path was added, false if it was a duplicate and not added.
     * @remarks If the STPathSet was constructed with the DeduplicationTag, then this method will
     *          check for duplicates and only add the path if it is not already present in the
     *          set. If the STPathSet was constructed without the DeduplicationTag,
     *          then this method will always add the path to the set, regardless of duplicates.
     *          It is fine for callers to ignore the return value.
     */
    bool
    pushBack(STPath const& e);

    /**
     * @brief emplaceBack adds a path to the set.
     *
     * @param args The arguments to construct the path with.
     * @return true if the path was added, false if it was a duplicate and not added.
     * @remarks If the STPathSet was constructed with the DeduplicationTag, then this method will
     *          check for duplicates and only add the path if it is not already present in the
     *          set. If the STPathSet was constructed without the DeduplicationTag,
     *          then this method will always add the path to the set, regardless of duplicates.
     *          It is fine for callers to ignore the return value.
     * @note The path is constructed before the duplicate check, so on a false
     *       return the constructed path is discarded and any argument
     *       forwarded as an rvalue is left in a moved-from state.  Use
     *       pushBack when the caller needs to keep its path on rejection.
     */
    template <typename... Args>
    bool
    emplaceBack(Args&&... args);

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    /**
     * @brief Append a path via `append`, then register it in the deduplication index.
     *
     * @param append Invoked with `value_`; must append exactly one path to it.
     * @return true if the path was kept, false if it was a duplicate and was rolled back.
     * @remarks Appends to the vector before touching the index, so that a failed allocation
     *          there leaves both containers untouched rather than leaving the index holding
     *          a path the vector does not.  If the index insert reports a duplicate, or
     *          throws, the append is rolled back so the two containers stay consistent; in
     *          the throwing case the exception propagates.  With no index (constructed
     *          without the DeduplicationTag) the append is unconditional.
     */
    template <typename Append>
    bool
    appendUnique(Append&& append);

    friend class detail::STVar;
};

// ------------ STPathElement ------------

inline STPathElement::STPathElement(
    std::optional<AccountID> const& account,
    std::optional<PathAsset> const& asset,
    std::optional<AccountID> const& issuer)
{
    if (account)
    {
        isOffer_ = false;
        accountID_ = *account;
        type_ |= STPathElementType::Account;
        XRPL_ASSERT(
            accountID_ != noAccount(), "xrpl::STPathElement::STPathElement : account is set");
    }

    if (asset)
    {
        assetID_ = *asset;
        type_ |= assetID_.holds<Currency>() ? STPathElementType::Currency : STPathElementType::Mpt;
    }

    if (issuer)
    {
        issuerID_ = *issuer;
        type_ |= STPathElementType::Issuer;
        XRPL_ASSERT(issuerID_ != noAccount(), "xrpl::STPathElement::STPathElement : issuer is set");
    }
}

inline STPathElement::STPathElement(
    AccountID const& account,
    PathAsset const& asset,
    AccountID const& issuer,
    bool forceAsset)
    : isOffer_(isXRP(account)), accountID_(account), assetID_(asset), issuerID_(issuer)
{
    if (!isOffer_)
        type_ |= STPathElementType::Account;

    if (forceAsset || !isXRP(assetID_))
        type_ |= asset.holds<Currency>() ? STPathElementType::Currency : STPathElementType::Mpt;

    if (!isXRP(issuer))
        type_ |= STPathElementType::Issuer;
}

inline STPathElement::STPathElement(
    STPathElementType type,
    AccountID const& account,
    PathAsset const& asset,
    AccountID const& issuer)
    : type_(type), isOffer_(isXRP(account)), accountID_(account), assetID_(asset), issuerID_(issuer)
{
    assetID_.visit(
        [&](Currency const&) { type_ &= ~STPathElementType::Mpt; },
        [&](MPTID const&) { type_ &= ~STPathElementType::Currency; });
}

inline STPathElementType
STPathElement::getNodeType() const noexcept
{
    return type_;
}

inline bool
STPathElement::isOffer() const noexcept
{
    return isOffer_;
}

inline bool
STPathElement::isAccount() const noexcept
{
    return !isOffer();
}

inline bool
STPathElement::isType(STPathElementType pe) const noexcept
{
    return (type_ & pe) != STPathElementType::None;
}

inline bool
STPathElement::hasIssuer() const noexcept
{
    return isType(STPathElementType::Issuer);
}

inline bool
STPathElement::hasCurrency() const noexcept
{
    return isType(STPathElementType::Currency);
}

inline bool
STPathElement::hasMPT() const noexcept
{
    return isType(STPathElementType::Mpt);
}

inline bool
STPathElement::hasAsset() const noexcept
{
    return isType(STPathElementType::Asset);
}

inline bool
STPathElement::isNone() const noexcept
{
    return getNodeType() == STPathElementType::None;
}

// Nodes are either an account ID or a offer prefix. Offer prefixs denote a
// class of offers.
inline AccountID const&
STPathElement::getAccountID() const noexcept
{
    return accountID_;
}

inline PathAsset const&
STPathElement::getPathAsset() const noexcept
{
    return assetID_;
}

inline Currency const&
STPathElement::getCurrency() const
{
    return assetID_.get<Currency>();
}

inline MPTID const&
STPathElement::getMPTID() const
{
    return assetID_.get<MPTID>();
}

inline AccountID const&
STPathElement::getIssuerID() const noexcept
{
    return issuerID_;
}

// ------------ STPath ------------

inline STPath::STPath(std::vector<STPathElement> p) : path_(std::move(p))
{
}

inline std::vector<STPathElement>::size_type
STPath::size() const
{
    return path_.size();
}

inline bool
STPath::empty() const
{
    return path_.empty();
}

inline void
STPath::pushBack(STPathElement const& e)
{
    path_.push_back(e);
}

template <typename... Args>
inline void
STPath::emplaceBack(Args&&... args)
{
    path_.emplace_back(std::forward<Args>(args)...);
}

inline std::vector<STPathElement>::const_iterator
STPath::begin() const
{
    return path_.begin();
}

inline std::vector<STPathElement>::const_iterator
STPath::end() const
{
    return path_.end();
}

inline bool
STPath::operator==(STPath const& t) const
{
    return path_ == t.path_;
}

inline std::vector<STPathElement>::const_reference
STPath::back() const
{
    return path_.back();
}

inline std::vector<STPathElement>::const_reference
STPath::front() const
{
    return path_.front();
}

inline STPathElement&
STPath::operator[](int i)
{
    return path_[i];
}

inline STPathElement const&
STPath::operator[](int i) const
{
    return path_[i];
}

inline void
STPath::reserve(size_t s)
{
    path_.reserve(s);
}

// ------------ STPathSet ------------

inline STPathSet::STPathSet(SField const& n) : STBase(n)
{
}

// std::vector like interface:
inline std::vector<STPath>::const_reference
STPathSet::operator[](std::vector<STPath>::size_type n) const
{
    return value_[n];
}

inline std::vector<STPath>::const_iterator
STPathSet::begin() const
{
    return value_.begin();
}

inline std::vector<STPath>::const_iterator
STPathSet::end() const
{
    return value_.end();
}

inline std::vector<STPath>::size_type
STPathSet::size() const
{
    return value_.size();
}

inline bool
STPathSet::empty() const
{
    return value_.empty();
}

template <typename Append>
inline bool
STPathSet::appendUnique(Append&& append)
{
    // Append to the vector first, so that a failed allocation there leaves both
    // containers untouched rather than leaving the index holding a path the
    // vector does not.
    append(value_);

    if (seen_ == nullptr)
    {
        return true;
    }

    try
    {
        if (!seen_->insert(value_.back()).second)
        {
            // Already present: roll back the append.
            value_.pop_back();
            return false;
        }
    }
    catch (...)
    {
        // The index insert failed, so roll back the append to keep the vector
        // and the index consistent.
        value_.pop_back();
        throw;
    }
    return true;
}

inline bool
STPathSet::pushBack(STPath const& e)
{
    return appendUnique([&](auto& value) { value.push_back(e); });
}

template <typename... Args>
inline bool
STPathSet::emplaceBack(Args&&... args)
{
    return appendUnique([&](auto& value) { value.emplace_back(std::forward<Args>(args)...); });
}

}  // namespace xrpl
