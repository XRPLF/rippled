#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

class STCurrency final : public STBase
{
private:
    Currency currency_;

public:
    using value_type = Currency;

    STCurrency() = default;

    explicit STCurrency(SerialIter& sit, SField const& name);

    explicit STCurrency(SField const& name, Currency const& currency);

    explicit STCurrency(SField const& name);

    [[nodiscard]] Currency const&
    currency() const;

    [[nodiscard]] Currency const&
    value() const noexcept;

    void
    setCurrency(Currency const& currency);

    [[nodiscard]] SerializedTypeID
    getSType() const override;

    [[nodiscard]] std::string
    getText() const override;

    [[nodiscard]] Json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    [[nodiscard]] bool
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

}  // namespace xrpl
