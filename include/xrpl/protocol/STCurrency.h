#ifndef XRPL_PROTOCOL_STCURRENCY_H_INCLUDED
#define XRPL_PROTOCOL_STCURRENCY_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

class STCurrency final : public STBase
{
private:
    Currency currency_{};

public:
    using value_type = Currency;

    STCurrency() = default;

    explicit STCurrency(SerialIter& sit, SField const& name);

    explicit STCurrency(SField const& name, Currency const& currency);

    explicit STCurrency(SField const& name);

    Currency const&
    currency() const;

    Currency const&
    value() const noexcept;

    void
    setCurrency(Currency const& currency);

    SerializedTypeID
    getSType() const override;

    std::string
    getText() const override;

    Json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(STBase const& t) const override;

    bool
    isDefault() const override;

private:
    static std::unique_ptr<STCurrency>
    construct(SerialIter&, SField const& name);

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

STCurrency
currencyFromJson(SField const& name, Json::Value const& v);

inline Currency const&
STCurrency::currency() const
{
    return currency_;
}

inline Currency const&
STCurrency::value() const noexcept
{
    return currency_;
}

inline void
STCurrency::setCurrency(Currency const& currency)
{
    currency_ = currency;
}

inline bool
operator==(STCurrency const& lhs, STCurrency const& rhs)
{
    return lhs.currency() == rhs.currency();
}

inline bool
operator!=(STCurrency const& lhs, STCurrency const& rhs)
{
    return !operator==(lhs, rhs);
}

inline bool
operator<(STCurrency const& lhs, STCurrency const& rhs)
{
    return lhs.currency() < rhs.currency();
}

inline bool
operator==(STCurrency const& lhs, Currency const& rhs)
{
    return lhs.currency() == rhs;
}

inline bool
operator<(STCurrency const& lhs, Currency const& rhs)
{
    return lhs.currency() < rhs;
}

}  // namespace ripple

#endif
