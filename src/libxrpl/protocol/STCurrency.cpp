#include <xrpl/protocol/STCurrency.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/basics/TraceLog.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

STCurrency::STCurrency(SField const& name) : STBase{name}
{
}

STCurrency::STCurrency(SerialIter& sit, SField const& name) : STBase{name}
{
    currency_ = sit.get160();
}

STCurrency::STCurrency(SField const& name, Currency const& currency)
    : STBase{name}, currency_{currency}
{
}

SerializedTypeID
STCurrency::getSType() const
{
    TRACE_FUNC();
    return STI_CURRENCY;
}

std::string
STCurrency::getText() const
{
    TRACE_FUNC();
    return to_string(currency_);
}

json::Value
STCurrency::getJson(JsonOptions) const
{
    TRACE_FUNC();
    return to_string(currency_);
}

void
STCurrency::add(Serializer& s) const
{
    TRACE_FUNC();
    s.addBitString(currency_);
}

bool
STCurrency::isEquivalent(STBase const& t) const
{
    TRACE_FUNC();
    STCurrency const* v = dynamic_cast<STCurrency const*>(&t);
    return (v != nullptr) && (*v == *this);
}

bool
STCurrency::isDefault() const
{
    TRACE_FUNC();
    return isXRP(currency_);
}

std::unique_ptr<STCurrency>
STCurrency::construct(SerialIter& sit, SField const& name)
{
    TRACE_FUNC();
    return std::make_unique<STCurrency>(sit, name);
}

STBase*
STCurrency::copy(std::size_t n, void* buf) const
{
    TRACE_FUNC();
    return emplace(n, buf, *this);
}

STBase*
STCurrency::move(std::size_t n, void* buf)
{
    TRACE_FUNC();
    return emplace(n, buf, std::move(*this));
}

STCurrency
currencyFromJson(SField const& name, json::Value const& v)
{
    TRACE_FUNC();
    if (!v.isString())
    {
        Throw<std::runtime_error>("currencyFromJson currency must be a string Json value");
    }

    auto const currency = toCurrency(v.asString());
    if (currency == badCurrency() || currency == noCurrency())
    {
        Throw<std::runtime_error>("currencyFromJson currency must be a valid currency");
    }

    return STCurrency{name, currency};
}

}  // namespace xrpl
