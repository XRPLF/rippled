#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace xrpl {

/* Represent STPathElement's asset, which can be Currency or MPTID.
 */
class PathAsset
{
private:
    std::variant<Currency, MPTID> easset_;

public:
    PathAsset() = default;
    // Enables comparing Asset and PathAsset
    PathAsset(Asset const& asset);
    PathAsset(Currency const& currency) : easset_(currency)
    {
    }
    PathAsset(MPTID const& mpt) : easset_(mpt)
    {
    }

    template <ValidPathAsset T>
    [[nodiscard]] constexpr bool
    holds() const;

    [[nodiscard]] constexpr bool
    isXRP() const;

    template <ValidPathAsset T>
    T const&
    get() const;

    [[nodiscard]] constexpr std::variant<Currency, MPTID> const&
    value() const;

    // Custom, generic visit implementation
    template <typename... Visitors>
    constexpr auto
    visit(Visitors&&... visitors) const -> decltype(auto)
    {
        // Simple delegation to the reusable utility, passing the internal
        // variant data.
        return detail::visit(easset_, std::forward<Visitors>(visitors)...);
    }

    friend constexpr bool
    operator==(PathAsset const& lhs, PathAsset const& rhs);
};

template <ValidPathAsset PA>
constexpr bool kIsCurrencyV = std::is_same_v<PA, Currency>;

template <ValidPathAsset PA>
constexpr bool kIsMptidV = std::is_same_v<PA, MPTID>;

inline PathAsset::PathAsset(Asset const& asset)
{
    asset.visit(
        [&](Issue const& issue) { easset_ = issue.currency; },
        [&](MPTIssue const& issue) { easset_ = issue.getMptID(); });
}

template <ValidPathAsset T>
constexpr bool
PathAsset::holds() const
{
    return std::holds_alternative<T>(easset_);
}

template <ValidPathAsset T>
[[nodiscard]] [[nodiscard]] T const&
PathAsset::get() const
{
    if (!holds<T>())
        Throw<std::runtime_error>("PathAsset doesn't hold requested asset.");
    return std::get<T>(easset_);
}

constexpr std::variant<Currency, MPTID> const&
PathAsset::value() const
{
    return easset_;
}

constexpr bool
PathAsset::isXRP() const
{
    return visit(
        [&](Currency const& currency) { return xrpl::isXRP(currency); },
        [](MPTID const&) { return false; });
}

constexpr bool
operator==(PathAsset const& lhs, PathAsset const& rhs)
{
    return std::visit(
        []<ValidPathAsset TLhs, ValidPathAsset TRhs>(TLhs const& lhs, TRhs const& rhs) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
            {
                return lhs == rhs;
            }
            else
            {
                return false;
            }
        },
        lhs.value(),
        rhs.value());
}

template <typename Hasher>
void
hash_append(Hasher& h, PathAsset const& pathAsset) noexcept
{
    using beast::hash_append;
    using Variant = std::remove_cvref_t<decltype(pathAsset.value())>;

    static_assert(
        std::variant_size_v<Variant> < 0xFFu,
        "PathAsset's discriminant must fit in a byte, leaving 0xFF reserved.");

    // std::visit is not noexcept: it throws bad_variant_access when the variant
    // is valueless_by_exception.
    if (pathAsset.value().valueless_by_exception()) [[unlikely]]
    {
        hash_append(h, static_cast<std::uint8_t>(0xFFu));
        return;
    }

    hash_append(h, static_cast<std::uint8_t>(pathAsset.value().index()));
    std::visit(
        [&]<ValidPathAsset T>(T const& e) noexcept {
            static_assert(
                noexcept(hash_append(h, e)),
                "Every PathAsset alternative must be nothrow-hashable.");
            hash_append(h, e);
        },
        pathAsset.value());
}

inline bool
isXRP(PathAsset const& asset)
{
    return asset.isXRP();
}

std::string
to_string(PathAsset const& asset);

std::ostream&
operator<<(std::ostream& os, PathAsset const& x);

}  // namespace xrpl
