#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>
#include <optional>

namespace xrpl {

class STPathElement final : public CountedObject<STPathElement>
{
    unsigned int mType_;
    AccountID mAccountID_;
    PathAsset mAssetID_;
    AccountID mIssuerID_;

    bool is_offer_;
    std::size_t hash_value_;

public:
    // Bitwise values (typeCurrency | typeMPT)
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
    enum Type {
        TypeNone = 0x00,
        TypeAccount = 0x01,   // Rippling through an account (vs taking an offer).
        TypeCurrency = 0x10,  // Currency follows.
        TypeIssuer = 0x20,    // Issuer follows.
        TypeMpt = 0x40,       // MPT follows.
        TypeBoundary = 0xFF,  // Boundary between alternate paths.
        TypeAsset = TypeCurrency | TypeMpt,
        TypeAll = TypeAccount | TypeCurrency | TypeIssuer | TypeMpt,
        // Combination of all types.
    };

    STPathElement();
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
        unsigned int uType,
        AccountID const& account,
        PathAsset const& asset,
        AccountID const& issuer);

    [[nodiscard]] auto
    getNodeType() const;

    [[nodiscard]] bool
    isOffer() const;

    [[nodiscard]] bool
    isAccount() const;

    [[nodiscard]] bool
    hasIssuer() const;

    [[nodiscard]] bool
    hasCurrency() const;

    [[nodiscard]] bool
    hasMPT() const;

    [[nodiscard]] bool
    hasAsset() const;

    [[nodiscard]] bool
    isNone() const;

    // Nodes are either an account ID or a offer prefix. Offer prefixs denote a
    // class of offers.
    [[nodiscard]] AccountID const&
    getAccountID() const;

    [[nodiscard]] PathAsset const&
    getPathAsset() const;

    [[nodiscard]] Currency const&
    getCurrency() const;

    [[nodiscard]] MPTID const&
    getMPTID() const;

    [[nodiscard]] AccountID const&
    getIssuerID() const;

    [[nodiscard]] bool
    isType(Type const& pe) const;

    bool
    operator==(STPathElement const& t) const;

    bool
    operator!=(STPathElement const& t) const;

private:
    static std::size_t
    getHash(STPathElement const& element);
};

class STPath final : public CountedObject<STPath>
{
    std::vector<STPathElement> mPath_;

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

    [[nodiscard]] Json::Value getJson(JsonOptions) const;

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

//------------------------------------------------------------------------------

// A set of zero or more payment paths
class STPathSet final : public STBase, public CountedObject<STPathSet>
{
    std::vector<STPath> value_;

public:
    STPathSet() = default;

    STPathSet(SField const& n);
    STPathSet(SerialIter& sit, SField const& name);

    void
    add(Serializer& s) const override;

    [[nodiscard]] Json::Value getJson(JsonOptions) const override;

    [[nodiscard]] SerializedTypeID
    getSType() const override;

    bool
    assembleAdd(STPath const& base, STPathElement const& tail);

    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    [[nodiscard]] bool
    isDefault() const override;

    // std::vector like interface:
    std::vector<STPath>::const_reference
    operator[](std::vector<STPath>::size_type n) const;

    std::vector<STPath>::reference
    operator[](std::vector<STPath>::size_type n);

    [[nodiscard]] std::vector<STPath>::const_iterator
    begin() const;

    [[nodiscard]] std::vector<STPath>::const_iterator
    end() const;

    [[nodiscard]] std::vector<STPath>::size_type
    size() const;

    [[nodiscard]] bool
    empty() const;

    void
    pushBack(STPath const& e);

    template <typename... Args>
    void
    emplaceBack(Args&&... args);

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

// ------------ STPathElement ------------

inline STPathElement::STPathElement() : mType_(TypeNone), is_offer_(true)
{
    hash_value_ = getHash(*this);
}

inline STPathElement::STPathElement(
    std::optional<AccountID> const& account,
    std::optional<PathAsset> const& asset,
    std::optional<AccountID> const& issuer)
    : mType_(TypeNone)
{
    if (!account)
    {
        is_offer_ = true;
    }
    else
    {
        is_offer_ = false;
        mAccountID_ = *account;
        mType_ |= TypeAccount;
        XRPL_ASSERT(
            mAccountID_ != noAccount(), "xrpl::STPathElement::STPathElement : account is set");
    }

    if (asset)
    {
        mAssetID_ = *asset;
        mType_ |= mAssetID_.holds<Currency>() ? TypeCurrency : TypeMpt;
    }

    if (issuer)
    {
        mIssuerID_ = *issuer;
        mType_ |= TypeIssuer;
        XRPL_ASSERT(
            mIssuerID_ != noAccount(), "xrpl::STPathElement::STPathElement : issuer is set");
    }

    hash_value_ = getHash(*this);
}

inline STPathElement::STPathElement(
    AccountID const& account,
    PathAsset const& asset,
    AccountID const& issuer,
    bool forceAsset)
    : mType_(TypeNone)
    , mAccountID_(account)
    , mAssetID_(asset)
    , mIssuerID_(issuer)
    , is_offer_(isXRP(mAccountID_))
{
    if (!is_offer_)
        mType_ |= TypeAccount;

    if (forceAsset || !isXRP(mAssetID_))
        mType_ |= asset.holds<Currency>() ? TypeCurrency : TypeMpt;

    if (!isXRP(issuer))
        mType_ |= TypeIssuer;

    hash_value_ = getHash(*this);
}

inline STPathElement::STPathElement(
    unsigned int uType,
    AccountID const& account,
    PathAsset const& asset,
    AccountID const& issuer)
    : mType_(uType)
    , mAccountID_(account)
    , mAssetID_(asset)
    , mIssuerID_(issuer)
    , is_offer_(isXRP(mAccountID_))
{
    // uType could be assetType; i.e. either Currency or MPTID.
    // Get the actual type.
    mAssetID_.visit(
        [&](Currency const&) { mType_ = mType_ & (~Type::TypeMpt); },
        [&](MPTID const&) { mType_ = mType_ & (~Type::TypeCurrency); });
    hash_value_ = getHash(*this);
}

inline auto
STPathElement::getNodeType() const
{
    return mType_;
}

inline bool
STPathElement::isOffer() const
{
    return is_offer_;
}

inline bool
STPathElement::isAccount() const
{
    return !isOffer();
}

inline bool
STPathElement::isType(Type const& pe) const
{
    return (mType_ & pe) != 0u;
}

inline bool
STPathElement::hasIssuer() const
{
    return isType(STPathElement::TypeIssuer);
}

inline bool
STPathElement::hasCurrency() const
{
    return isType(STPathElement::TypeCurrency);
}

inline bool
STPathElement::hasMPT() const
{
    return isType(STPathElement::TypeMpt);
}

inline bool
STPathElement::hasAsset() const
{
    return isType(STPathElement::TypeAsset);
}

inline bool
STPathElement::isNone() const
{
    return getNodeType() == STPathElement::TypeNone;
}

// Nodes are either an account ID or a offer prefix. Offer prefixs denote a
// class of offers.
inline AccountID const&
STPathElement::getAccountID() const
{
    return mAccountID_;
}

inline PathAsset const&
STPathElement::getPathAsset() const
{
    return mAssetID_;
}

inline Currency const&
STPathElement::getCurrency() const
{
    return mAssetID_.get<Currency>();
}

inline MPTID const&
STPathElement::getMPTID() const
{
    return mAssetID_.get<MPTID>();
}

inline AccountID const&
STPathElement::getIssuerID() const
{
    return mIssuerID_;
}

inline bool
STPathElement::operator==(STPathElement const& t) const
{
    return (mType_ & TypeAccount) == (t.mType_ & TypeAccount) && hash_value_ == t.hash_value_ &&
        mAccountID_ == t.mAccountID_ && mAssetID_ == t.mAssetID_ && mIssuerID_ == t.mIssuerID_;
}

inline bool
STPathElement::operator!=(STPathElement const& t) const
{
    return !operator==(t);
}

// ------------ STPath ------------

inline STPath::STPath(std::vector<STPathElement> p) : mPath_(std::move(p))
{
}

inline std::vector<STPathElement>::size_type
STPath::size() const
{
    return mPath_.size();
}

inline bool
STPath::empty() const
{
    return mPath_.empty();
}

inline void
STPath::pushBack(STPathElement const& e)
{
    mPath_.push_back(e);
}

template <typename... Args>
inline void
STPath::emplaceBack(Args&&... args)
{
    mPath_.emplace_back(std::forward<Args>(args)...);
}

inline std::vector<STPathElement>::const_iterator
STPath::begin() const
{
    return mPath_.begin();
}

inline std::vector<STPathElement>::const_iterator
STPath::end() const
{
    return mPath_.end();
}

inline bool
STPath::operator==(STPath const& t) const
{
    return mPath_ == t.mPath_;
}

inline std::vector<STPathElement>::const_reference
STPath::back() const
{
    return mPath_.back();
}

inline std::vector<STPathElement>::const_reference
STPath::front() const
{
    return mPath_.front();
}

inline STPathElement&
STPath::operator[](int i)
{
    return mPath_[i];
}

inline STPathElement const&
STPath::operator[](int i) const
{
    return mPath_[i];
}

inline void
STPath::reserve(size_t s)
{
    mPath_.reserve(s);
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

inline std::vector<STPath>::reference
STPathSet::operator[](std::vector<STPath>::size_type n)
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

inline void
STPathSet::pushBack(STPath const& e)
{
    value_.push_back(e);
}

template <typename... Args>
inline void
STPathSet::emplaceBack(Args&&... args)
{
    value_.emplace_back(std::forward<Args>(args)...);
}

}  // namespace xrpl
