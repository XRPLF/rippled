#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstddef>
#include <optional>

namespace xrpl {

class STPathElement final : public CountedObject<STPathElement>
{
    unsigned int type_;
    AccountID accountID_;
    Currency currencyID_;
    AccountID issuerID_;

    bool is_offer_;
    std::size_t hash_value_;

public:
    enum Type {
        typeNone = 0x00,
        typeAccount = 0x01,   // Rippling through an account (vs taking an offer).
        typeCurrency = 0x10,  // Currency follows.
        typeIssuer = 0x20,    // Issuer follows.
        typeBoundary = 0xFF,  // Boundary between alternate paths.
        typeAll = typeAccount | typeCurrency | typeIssuer,
        // Combination of all types.
    };

    STPathElement();
    STPathElement(STPathElement const&) = default;
    STPathElement&
    operator=(STPathElement const&) = default;

    STPathElement(
        std::optional<AccountID> const& account,
        std::optional<Currency> const& currency,
        std::optional<AccountID> const& issuer);

    STPathElement(
        AccountID const& account,
        Currency const& currency,
        AccountID const& issuer,
        bool forceCurrency = false);

    STPathElement(
        unsigned int uType,
        AccountID const& account,
        Currency const& currency,
        AccountID const& issuer);

    auto
    getNodeType() const;

    bool
    isOffer() const;

    bool
    isAccount() const;

    bool
    hasIssuer() const;

    bool
    hasCurrency() const;

    bool
    isNone() const;

    // Nodes are either an account ID or a offer prefix. Offer prefixs denote a
    // class of offers.
    AccountID const&
    getAccountID() const;

    Currency const&
    getCurrency() const;

    AccountID const&
    getIssuerID() const;

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
    std::vector<STPathElement> path_;

public:
    STPath() = default;

    STPath(std::vector<STPathElement> p);

    std::vector<STPathElement>::size_type
    size() const;

    bool
    empty() const;

    void
    push_back(STPathElement const& e);

    template <typename... Args>
    void
    emplace_back(Args&&... args);

    bool
    hasSeen(AccountID const& account, Currency const& currency, AccountID const& issuer) const;

    Json::Value getJson(JsonOptions) const;

    std::vector<STPathElement>::const_iterator
    begin() const;

    std::vector<STPathElement>::const_iterator
    end() const;

    bool
    operator==(STPath const& t) const;

    std::vector<STPathElement>::const_reference
    back() const;

    std::vector<STPathElement>::const_reference
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
    std::vector<STPath> value;

public:
    STPathSet() = default;

    STPathSet(SField const& n);
    STPathSet(SerialIter& sit, SField const& name);

    void
    add(Serializer& s) const override;

    Json::Value getJson(JsonOptions) const override;

    SerializedTypeID
    getSType() const override;

    bool
    assembleAdd(STPath const& base, STPathElement const& tail);

    bool
    isEquivalent(STBase const& t) const override;

    bool
    isDefault() const override;

    // std::vector like interface:
    std::vector<STPath>::const_reference
    operator[](std::vector<STPath>::size_type n) const;

    std::vector<STPath>::reference
    operator[](std::vector<STPath>::size_type n);

    std::vector<STPath>::const_iterator
    begin() const;

    std::vector<STPath>::const_iterator
    end() const;

    std::vector<STPath>::size_type
    size() const;

    bool
    empty() const;

    void
    push_back(STPath const& e);

    template <typename... Args>
    void
    emplace_back(Args&&... args);

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

// ------------ STPathElement ------------

inline STPathElement::STPathElement() : type_(typeNone), is_offer_(true)
{
    hash_value_ = getHash(*this);
}

inline STPathElement::STPathElement(
    std::optional<AccountID> const& account,
    std::optional<Currency> const& currency,
    std::optional<AccountID> const& issuer)
    : type_(typeNone)
{
    if (!account)
    {
        is_offer_ = true;
    }
    else
    {
        is_offer_ = false;
        accountID_ = *account;
        type_ |= typeAccount;
        XRPL_ASSERT(
            accountID_ != noAccount(), "xrpl::STPathElement::STPathElement : account is set");
    }

    if (currency)
    {
        currencyID_ = *currency;
        type_ |= typeCurrency;
    }

    if (issuer)
    {
        issuerID_ = *issuer;
        type_ |= typeIssuer;
        XRPL_ASSERT(issuerID_ != noAccount(), "xrpl::STPathElement::STPathElement : issuer is set");
    }

    hash_value_ = getHash(*this);
}

inline STPathElement::STPathElement(
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer,
    bool forceCurrency)
    : type_(typeNone)
    , accountID_(account)
    , currencyID_(currency)
    , issuerID_(issuer)
    , is_offer_(isXRP(accountID_))
{
    if (!is_offer_)
        type_ |= typeAccount;

    if (forceCurrency || !isXRP(currency))
        type_ |= typeCurrency;

    if (!isXRP(issuer))
        type_ |= typeIssuer;

    hash_value_ = getHash(*this);
}

inline STPathElement::STPathElement(
    unsigned int uType,
    AccountID const& account,
    Currency const& currency,
    AccountID const& issuer)
    : type_(uType)
    , accountID_(account)
    , currencyID_(currency)
    , issuerID_(issuer)
    , is_offer_(isXRP(accountID_))
{
    hash_value_ = getHash(*this);
}

inline auto
STPathElement::getNodeType() const
{
    return type_;
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
STPathElement::hasIssuer() const
{
    return getNodeType() & STPathElement::typeIssuer;
}

inline bool
STPathElement::hasCurrency() const
{
    return getNodeType() & STPathElement::typeCurrency;
}

inline bool
STPathElement::isNone() const
{
    return getNodeType() == STPathElement::typeNone;
}

// Nodes are either an account ID or a offer prefix. Offer prefixs denote a
// class of offers.
inline AccountID const&
STPathElement::getAccountID() const
{
    return accountID_;
}

inline Currency const&
STPathElement::getCurrency() const
{
    return currencyID_;
}

inline AccountID const&
STPathElement::getIssuerID() const
{
    return issuerID_;
}

inline bool
STPathElement::operator==(STPathElement const& t) const
{
    return (type_ & typeAccount) == (t.type_ & typeAccount) && hash_value_ == t.hash_value_ &&
        accountID_ == t.accountID_ && currencyID_ == t.currencyID_ && issuerID_ == t.issuerID_;
}

inline bool
STPathElement::operator!=(STPathElement const& t) const
{
    return !operator==(t);
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
STPath::push_back(STPathElement const& e)
{
    path_.push_back(e);
}

template <typename... Args>
inline void
STPath::emplace_back(Args&&... args)
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
    return value[n];
}

inline std::vector<STPath>::reference
STPathSet::operator[](std::vector<STPath>::size_type n)
{
    return value[n];
}

inline std::vector<STPath>::const_iterator
STPathSet::begin() const
{
    return value.begin();
}

inline std::vector<STPath>::const_iterator
STPathSet::end() const
{
    return value.end();
}

inline std::vector<STPath>::size_type
STPathSet::size() const
{
    return value.size();
}

inline bool
STPathSet::empty() const
{
    return value.empty();
}

inline void
STPathSet::push_back(STPath const& e)
{
    value.push_back(e);
}

template <typename... Args>
inline void
STPathSet::emplace_back(Args&&... args)
{
    value.emplace_back(std::forward<Args>(args)...);
}

}  // namespace xrpl
