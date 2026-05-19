#pragma once

#include <xrpl/protocol/UintTypes.h>

#include <type_traits>

namespace xrpl {

class STAmount;
class Asset;
class Issue;
class MPTIssue;
class IOUAmount;
class XRPAmount;
class MPTAmount;

template <typename A>
concept StepAmount =
    std::is_same_v<A, XRPAmount> || std::is_same_v<A, IOUAmount> || std::is_same_v<A, MPTAmount>;

template <typename TIss>
concept ValidIssueType = std::is_same_v<TIss, Issue> || std::is_same_v<TIss, MPTIssue>;

template <typename A>
concept AssetType = std::is_convertible_v<A, Asset> || std::is_convertible_v<A, Issue> ||
    std::is_convertible_v<A, MPTIssue> || std::is_convertible_v<A, MPTID>;

template <typename T>
concept ValidPathAsset = (std::is_same_v<T, Currency> || std::is_same_v<T, MPTID>);

template <class TTakerPays, class TTakerGets>
concept ValidTaker =
    ((std::is_same_v<TTakerPays, IOUAmount> || std::is_same_v<TTakerPays, XRPAmount> ||
      std::is_same_v<TTakerPays, MPTAmount>) &&
     (std::is_same_v<TTakerGets, IOUAmount> || std::is_same_v<TTakerGets, XRPAmount> ||
      std::is_same_v<TTakerGets, MPTAmount>) &&
     (!std::is_same_v<TTakerPays, XRPAmount> || !std::is_same_v<TTakerGets, XRPAmount>));

namespace detail {

// This template combines multiple callable objects (lambdas) into a single
// object that std::visit can use for overload resolution.
template <typename... Ts>
struct CombineVisitors : Ts...
{
    // Bring all operator() overloads from base classes into this scope.
    // It's the mechanism that makes the CombineVisitors struct function
    // as a single callable object with multiple overloads.
    using Ts::operator()...;

    // Perfect forwarding constructor to correctly initialize the base class
    // lambdas
    constexpr CombineVisitors(Ts&&... ts) : Ts(std::forward<Ts>(ts))...
    {
    }
};

// This function forces function template argument deduction, which is more
// robust than class template argument deduction (CTAD) via the deduction guide.
template <typename... Ts>
constexpr CombineVisitors<std::decay_t<Ts>...>
make_combine_visitors(Ts&&... ts)
{
    // std::decay_t<Ts> is used to remove references/constness from the lambda
    // types before they are passed as template arguments to the CombineVisitors
    // struct.
    return CombineVisitors<std::decay_t<Ts>...>{std::forward<Ts>(ts)...};
}

// This function takes ANY variant and ANY number of visitors, and performs the
// visit. It is the reusable core logic.
template <typename Variant, typename... Visitors>
constexpr auto
visit(Variant&& v, Visitors&&... visitors) -> decltype(auto)
{
    // Use the function template helper instead of raw CTAD.
    auto visitor_set = make_combine_visitors(std::forward<Visitors>(visitors)...);

    // Delegate to std::visit, perfectly forwarding the variant and the visitor
    // set.
    return std::visit(visitor_set, std::forward<Variant>(v));
}

}  // namespace detail

}  // namespace xrpl
