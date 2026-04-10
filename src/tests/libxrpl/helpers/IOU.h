#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>

#include <helpers/Account.h>

#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>

namespace xrpl::test {

/**
 * @brief Represents an IOU (issued currency) for testing.
 *
 * Provides a clear, explicit API for creating currencies issued by an account.
 * This replaces the cryptic `Account::operator[]` from the jtx framework.
 *
 * @code
 *     Account gw("gateway");
 *     IOU USD("USD", gw);
 *
 *     auto issue = USD.issue();      // Get the Issue
 *     auto asset = USD.asset();      // Get the Asset
 *     auto amt = USD.amount(100);    // Get STAmount of 100 USD
 * @endcode
 */
class IOU
{
public:
    /**
     * @brief Construct an IOU from a currency code and issuing account.
     * @param currencyCode A 3-character ISO currency code (e.g., "USD").
     * @param issuer The account that issues this currency.
     */
    IOU(std::string_view currencyCode, Account const& issuer)
        : currency_(to_currency(std::string(currencyCode))), issuer_(issuer.id())
    {
        XRPL_ASSERT(!isXRP(currency_), "IOU: currency code must not resolve to XRP");
    }

    /**
     * @brief Construct an IOU from a Currency and issuing account.
     * @param currency The Currency object.
     * @param issuer The account that issues this currency.
     */
    IOU(Currency currency, Account const& issuer)
        : currency_(std::move(currency)), issuer_(issuer.id())
    {
        XRPL_ASSERT(!isXRP(currency_), "IOU: currency code must not resolve to XRP");
    }

    /**
     * @brief Get the Issue (currency + issuer pair).
     * @return An Issue object representing this IOU.
     */
    [[nodiscard]] Issue
    issue() const
    {
        return Issue{currency_, issuer_};
    }

    /**
     * @brief Get the Asset.
     * @return An Asset object representing this IOU.
     */
    [[nodiscard]] Asset
    asset() const
    {
        return Asset{issue()};
    }

    /**
     * @brief Create an STAmount of this IOU.
     *
     * Works with any arithmetic type (int, double, etc.) by converting
     * to string and parsing. This matches the jtx IOU behaviour.
     *
     * @tparam T An arithmetic type.
     * @param value The amount as any arithmetic type.
     * @return An STAmount representing value units of this IOU.
     */
    template <typename T>
        requires std::is_arithmetic_v<T>
    [[nodiscard]] STAmount
    amount(T value) const
    {
        return amountFromString(issue(), to_string(value));
    }

    /**
     * @brief Create an STAmount of this IOU from a Number.
     * @param value The amount as a Number.
     * @return An STAmount representing value units of this IOU.
     */
    [[nodiscard]] STAmount
    amount(Number const& value) const
    {
        return STAmount{issue(), value};
    }

    /**
     * @brief Get the currency.
     * @return The currency.
     */
    [[nodiscard]] Currency const&
    currency() const
    {
        return currency_;
    }

    /**
     * @brief Get the issuer account ID.
     * @return The issuer's AccountID.
     */
    [[nodiscard]] AccountID const&
    issuer() const
    {
        return issuer_;
    }

private:
    Currency currency_;
    AccountID issuer_;
};

}  // namespace xrpl::test
