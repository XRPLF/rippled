/**
 * @file Asset.h
 * @brief Unified asset abstraction for XRP, IOU, and MPT value types.
 *
 * Introduces the `Asset` type, a `std::variant<Issue, MPTIssue>` wrapper that
 * represents all three kinds of transferable value on the XRP Ledger: native
 * XRP, IOU issued currencies, and Multi-Purpose Tokens (MPT). `Issue` covers
 * both XRP and IOU (distinguished by `Issue::native()`), so the variant has
 * two arms but three logical asset kinds.
 *
 * Conversions *to* `Asset` are implicit (from `Issue`, `MPTIssue`, or `MPTID`)
 * to preserve backward compatibility with legacy `Issue`-taking APIs.
 * Conversions *out* are explicit via `get<TIss>()` or guarded with `holds<TIss>()`.
 */

#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rules.h>

namespace xrpl {

class STAmount;

/**
 * @brief Empty tag type encoding an amount's numeric kind as a template
 *     parameter.
 *
 * Carries no data; its sole purpose is to convey compile-time type information
 * through a runtime `std::variant`. Code that needs to dispatch on the numeric
 * kind of an `Asset` calls `Asset::getAmountType()`, which returns a
 * `std::variant<AmountType<XRPAmount>, AmountType<IOUAmount>,
 * AmountType<MPTAmount>>`, and then `std::visit`s over it to select the
 * correct templated path.
 *
 * @tparam T Must be one of `XRPAmount`, `IOUAmount`, or `MPTAmount`.
 */
template <typename T>
    requires(
        std::is_same_v<T, XRPAmount> || std::is_same_v<T, IOUAmount> ||
        std::is_same_v<T, MPTAmount>)
struct AmountType
{
    using amount_type = T;
};

/**
 * @brief Sentinel tag used to test whether an `Asset` holds an invalid value.
 *
 * An `Asset` is "bad" when it holds an `Issue` whose currency equals
 * `badCurrency()`, or an `MPTIssue` whose issuer equals `xrpAccount()` (the
 * zero-account sentinel). Use `operator==(BadAsset const&, Asset const&)` or
 * compare against `badAsset()` rather than inspecting the sub-type directly.
 *
 * This pattern avoids a separate validity flag or `std::optional<Asset>`:
 * invalid states are represented as well-known sentinel values.
 */
struct BadAsset
{
};

/**
 * @brief Returns a reference to the singleton `BadAsset` sentinel.
 *
 * Prefer `badAsset() == myAsset` over constructing a temporary `BadAsset{}`.
 */
inline BadAsset const&
badAsset()
{
    static BadAsset const kA;
    return kA;
}

/**
 * @brief Unified representation of an XRP Ledger asset: XRP, IOU, or MPT.
 *
 * Wraps `std::variant<Issue, MPTIssue>`. Because `Issue` already encodes both
 * XRP (via `Issue::native()`) and IOU, the variant has two arms but three
 * logical asset kinds. Value semantics and `constexpr` comparisons are
 * preserved — no vtables, no heap allocation.
 *
 * Implicit conversions *from* `Issue`, `MPTIssue`, and `MPTID` allow callers
 * to pass those types anywhere an `Asset` is expected. Extraction of the
 * concrete sub-type is explicit: guard with `holds<TIss>()` then call
 * `get<TIss>()`, or use `visit()` for exhaustive dispatch.
 *
 * `STAmount` stores an `Asset` as its type-identity half and delegates
 * `native()`, `integral()`, `holds<>()`, and `get<>()` directly to it.
 */
class Asset
{
public:
    /** Underlying storage type: one of `Issue` (XRP or IOU) or `MPTIssue`. */
    using value_type = std::variant<Issue, MPTIssue>;
    /** Currency or MPTID, depending on the active arm. */
    using token_type = std::variant<Currency, MPTID>;
    /** Runtime amount-kind discriminant returned by `getAmountType()`. */
    using AmtType =
        std::variant<AmountType<XRPAmount>, AmountType<IOUAmount>, AmountType<MPTAmount>>;

private:
    value_type issue_;

public:
    /** Constructs a default (XRP) asset. */
    Asset() = default;

    /**
     * @brief Constructs an Asset from an `Issue` (XRP or IOU).
     *
     * Implicit to preserve backward compatibility with APIs that previously
     * accepted `Issue` directly.
     *
     * @param issue The XRP or IOU issue to wrap.
     */
    Asset(Issue const& issue) : issue_(issue)
    {
    }

    /**
     * @brief Constructs an Asset from an `MPTIssue`.
     *
     * Implicit so callers can pass an `MPTIssue` wherever `Asset` is expected.
     *
     * @param mptIssue The MPT issuance to wrap.
     */
    Asset(MPTIssue const& mptIssue) : issue_(mptIssue)
    {
    }

    /**
     * @brief Constructs an Asset from a raw `MPTID`.
     *
     * Convenience implicit conversion that wraps the issuance ID in an
     * `MPTIssue` before storing it.
     *
     * @param issuanceID The 192-bit MPT issuance identifier.
     */
    Asset(MPTID const& issuanceID) : issue_(MPTIssue{issuanceID})
    {
    }

    /**
     * @brief Returns the issuer of this asset.
     *
     * For XRP, returns the zero `AccountID` (no real issuer). For IOU, returns
     * the issuing account. For MPT, returns the sequence-owner encoded in the
     * MPTID.
     */
    [[nodiscard]] AccountID const&
    getIssuer() const;

    /**
     * @brief Returns a const reference to the active sub-type.
     *
     * @tparam TIss `Issue` or `MPTIssue`.
     * @throws std::logic_error if the asset does not hold `TIss`. Guard with
     *     `holds<TIss>()` before calling, or use `visit()` for exhaustive
     *     dispatch.
     */
    template <ValidIssueType TIss>
    constexpr TIss const&
    get() const;

    /**
     * @brief Returns a mutable reference to the active sub-type.
     *
     * @tparam TIss `Issue` or `MPTIssue`.
     * @throws std::logic_error if the asset does not hold `TIss`.
     */
    template <ValidIssueType TIss>
    TIss&
    get();

    /**
     * @brief Tests whether the asset currently holds the given sub-type.
     *
     * @tparam TIss `Issue` or `MPTIssue`.
     * @return `true` if the active arm matches `TIss`.
     */
    template <ValidIssueType TIss>
    [[nodiscard]] constexpr bool
    holds() const;

    /**
     * @brief Returns a human-readable string identifying the asset.
     *
     * Delegates to the underlying `Issue` or `MPTIssue` text representation.
     */
    [[nodiscard]] std::string
    getText() const;

    /**
     * @brief Returns a const reference to the underlying `variant` storage.
     *
     * Prefer `visit()` or `get<TIss>()` for type-safe access; this accessor
     * is available for callers that must interact with the variant directly.
     */
    [[nodiscard]] constexpr value_type const&
    value() const;

    /**
     * @brief Returns the currency token identity of this asset.
     *
     * For XRP and IOU assets, returns the `Currency`. For MPT assets, returns
     * the `MPTID`. Useful when identity must be compared independently of the
     * issuer.
     */
    [[nodiscard]] constexpr token_type
    token() const;

    /**
     * @brief Serializes the asset into a JSON value.
     *
     * For IOU: emits `currency` and `issuer` keys (no issuer for XRP).
     * For MPT: emits `mpt_issuance_id`.
     *
     * @param jv Output JSON object; populated in place.
     */
    void
    setJson(json::Value& jv) const;

    /**
     * @brief Constructs an `STAmount` from this asset and a raw numeric value.
     *
     * Convenience operator enabling concise amount construction:
     * `myAsset(someNumber)`. The `Number` is interpreted according to the
     * asset's kind (XRP drops, IOU mantissa/exponent, MPT integer).
     *
     * @param n The numeric value to associate with this asset.
     * @return An `STAmount` holding this asset and the given value.
     */
    STAmount
    operator()(Number const&) const;

    /**
     * @brief Returns a tag-variant encoding the runtime amount kind.
     *
     * The returned variant holds one of `AmountType<XRPAmount>`,
     * `AmountType<IOUAmount>`, or `AmountType<MPTAmount>`. `std::visit` over
     * this result to select the correct templated arithmetic path without
     * inspecting the asset sub-type manually.
     */
    [[nodiscard]] constexpr AmtType
    getAmountType() const;

    /**
     * @brief Applies a set of lambdas to the active `Issue` or `MPTIssue` arm.
     *
     * Combines the provided callables into a single overload set using
     * `detail::visit` (the `CombineVisitors` trick from `Concepts.h`) and
     * forwards to `std::visit` over the internal variant. Example:
     * @code
     * asset.visit(
     *     [](Issue const& issue) { / * XRP or IOU * / },
     *     [](MPTIssue const& mpt) { / * MPT * / });
     * @endcode
     *
     * @tparam Visitors Callable types whose signatures cover `Issue` and `MPTIssue`.
     * @return The return value of the matching visitor.
     */
    template <typename... Visitors>
    constexpr auto
    visit(Visitors&&... visitors) const -> decltype(auto)
    {
        return detail::visit(issue_, std::forward<Visitors>(visitors)...);
    }

    /**
     * @brief Returns `true` if and only if the asset is native XRP.
     *
     * MPT always returns `false`; IOU always returns `false`; only the XRP
     * arm of `Issue` returns `true`.
     */
    [[nodiscard]] constexpr bool
    native() const
    {
        return visit(
            [&](Issue const& issue) { return issue.native(); },
            [&](MPTIssue const&) { return false; });
    }

    /**
     * @brief Returns `true` if the asset has an integer (non-fractional) amount
     *     representation.
     *
     * Both XRP (drops) and MPT amounts are always whole numbers. IOU amounts
     * use a floating-point mantissa/exponent encoding and are not integral.
     * This distinction affects serialization and arithmetic rounding.
     */
    [[nodiscard]] bool
    integral() const
    {
        return visit(
            [&](Issue const& issue) { return issue.native(); },
            [&](MPTIssue const&) { return true; });
    }

    /**
     * @brief Equality: `true` when both assets hold the same sub-type and
     *     compare equal within that sub-type.
     *
     * Cross-type comparisons (e.g., `Issue` vs `MPTIssue`) always return
     * `false`. For IOU, both currency and issuer must match. Use
     * `equalTokens()` to compare ignoring issuer.
     */
    friend constexpr bool
    operator==(Asset const& lhs, Asset const& rhs);

    /**
     * @brief Total order over assets for use in sorted containers.
     *
     * When both assets hold the same variant arm, ordering is delegated to
     * that arm's natural `<=>`. When arms differ, `Issue` sorts greater than
     * `MPTIssue` (an arbitrary but stable convention).
     */
    friend constexpr std::weak_ordering
    operator<=>(Asset const& lhs, Asset const& rhs);

    /**
     * @brief Tests whether the asset holds an `Issue` with the given currency.
     *
     * Returns `false` for any `MPTIssue` asset regardless of `lhs`.
     *
     * @param lhs The currency to compare against.
     * @param rhs The asset to inspect.
     */
    friend constexpr bool
    operator==(Currency const& lhs, Asset const& rhs);

    /**
     * @brief Tests whether the asset represents an invalid (sentinel) value.
     *
     * Returns `true` when `rhs` holds an `Issue` with `badCurrency()`, or an
     * `MPTIssue` whose issuer is `xrpAccount()` (the zero-account sentinel).
     *
     * @param lhs Unused sentinel tag; use `badAsset()` as the left operand.
     * @param rhs The asset to test.
     */
    friend constexpr bool
    operator==(BadAsset const& lhs, Asset const& rhs);

    /**
     * @brief Returns `true` if both assets refer to the same token type,
     *     regardless of issuer.
     *
     * For `Issue`-vs-`Issue` comparisons only the `Currency` field is checked;
     * issuers are ignored. For `MPTIssue`-vs-`MPTIssue` the full `MPTID` is
     * compared (issuer is already encoded in the ID, so there is no
     * issuer-free concept). Cross-type comparisons always return `false`.
     *
     * Used in path-finding and offer-matching where token type must match but
     * trust lines from different issuers in the same currency are acceptable.
     */
    friend constexpr bool
    equalTokens(Asset const& lhs, Asset const& rhs);
};

/** @brief `true` when `TIss` is `Issue`. Helper for `operator<=>`. */
template <ValidIssueType TIss>
constexpr bool kIS_ISSUE_V = std::is_same_v<TIss, Issue>;

/** @brief `true` when `TIss` is `MPTIssue`. Helper for `operator<=>`. */
template <ValidIssueType TIss>
constexpr bool kIS_MPTISSUE_V = std::is_same_v<TIss, MPTIssue>;

/**
 * @brief Converts an asset to a `json::Value` representation.
 *
 * For IOU: produces `{currency, issuer}` (no issuer key for XRP).
 * For MPT: produces `{mpt_issuance_id}`.
 *
 * @param asset The asset to serialize.
 * @return A `json::Value` object describing the asset.
 */
inline json::Value
toJson(Asset const& asset)
{
    json::Value jv;
    asset.setJson(jv);
    return jv;
}

template <ValidIssueType TIss>
constexpr bool
Asset::holds() const
{
    return std::holds_alternative<TIss>(issue_);
}

template <ValidIssueType TIss>
[[nodiscard]] constexpr TIss const&
Asset::get() const
{
    if (!std::holds_alternative<TIss>(issue_))
        Throw<std::logic_error>("Asset is not a requested issue");
    return std::get<TIss>(issue_);
}

template <ValidIssueType TIss>
TIss&
Asset::get()
{
    if (!std::holds_alternative<TIss>(issue_))
        Throw<std::logic_error>("Asset is not a requested issue");
    return std::get<TIss>(issue_);
}

constexpr Asset::value_type const&
Asset::value() const
{
    return issue_;
}

constexpr Asset::token_type
Asset::token() const
{
    return visit(
        [&](Issue const& issue) -> Asset::token_type { return issue.currency; },
        [&](MPTIssue const& issue) -> Asset::token_type { return issue.getMptID(); });
}

constexpr Asset::AmtType
Asset::getAmountType() const
{
    return visit(
        [&](Issue const& issue) -> Asset::AmtType {
            constexpr AmountType<XRPAmount> kXRP;
            constexpr AmountType<IOUAmount> kIOU;
            return native() ? AmtType(kXRP) : AmtType(kIOU);
        },
        [&](MPTIssue const& issue) -> Asset::AmtType {
            constexpr AmountType<MPTAmount> kMPT;
            return AmtType(kMPT);
        });
}

constexpr bool
operator==(Asset const& lhs, Asset const& rhs)
{
    return std::visit(
        [&]<typename TLhs, typename TRhs>(TLhs const& issLhs, TRhs const& issRhs) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
            {
                return issLhs == issRhs;
            }
            else
            {
                return false;
            }
        },
        lhs.issue_,
        rhs.issue_);
}

constexpr std::weak_ordering
operator<=>(Asset const& lhs, Asset const& rhs)
{
    return std::visit(
        []<ValidIssueType TLhs, ValidIssueType TRhs>(TLhs const& lhs, TRhs const& rhs) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
            {
                return std::weak_ordering(lhs <=> rhs);
            }
            else if constexpr (kIS_ISSUE_V<TLhs> && kIS_MPTISSUE_V<TRhs>)
            {
                return std::weak_ordering::greater;
            }
            else
            {
                return std::weak_ordering::less;
            }
        },
        lhs.issue_,
        rhs.issue_);
}

constexpr bool
operator==(Currency const& lhs, Asset const& rhs)
{
    return rhs.visit(
        [&](Issue const& issue) { return issue.currency == lhs; },
        [](MPTIssue const& issue) { return false; });
}

constexpr bool
operator==(BadAsset const&, Asset const& rhs)
{
    return rhs.visit(
        [](Issue const& issue) -> bool { return badCurrency() == issue.currency; },
        [](MPTIssue const& issue) -> bool { return issue.getIssuer() == xrpAccount(); });
}

constexpr bool
equalTokens(Asset const& lhs, Asset const& rhs)
{
    return std::visit(
        [&]<typename TLhs, typename TRhs>(TLhs const& issLhs, TRhs const& issRhs) {
            if constexpr (std::is_same_v<TLhs, Issue> && std::is_same_v<TRhs, Issue>)
            {
                return issLhs.currency == issRhs.currency;
            }
            else if constexpr (std::is_same_v<TLhs, MPTIssue> && std::is_same_v<TRhs, MPTIssue>)
            {
                return issLhs.getMptID() == issRhs.getMptID();
            }
            else
            {
                return false;
            }
        },
        lhs.issue_,
        rhs.issue_);
}

/**
 * @brief Returns `true` if the asset is native XRP.
 *
 * Thin wrapper around `Asset::native()` for readability at call sites.
 *
 * @param asset The asset to test.
 */
inline bool
isXRP(Asset const& asset)
{
    return asset.native();
}

/**
 * @brief Returns a human-readable string representation of an asset.
 *
 * Delegates to the underlying `Issue` or `MPTIssue` text form. Suitable for
 * logging and error messages; not for wire serialization.
 *
 * @param asset The asset to stringify.
 * @return A descriptive string identifying the asset.
 */
std::string
to_string(Asset const& asset);

/**
 * @brief Validates that a JSON object encodes a well-formed asset.
 *
 * Enforces the protocol rule that an asset JSON object must contain exactly
 * one of `currency` or `mpt_issuance_id`, but not both.
 *
 * @param jv The JSON value to validate.
 * @return `true` if the JSON represents a valid asset; `false` otherwise.
 */
bool
validJSONAsset(json::Value const& jv);

/**
 * @brief Parses an `Asset` from a JSON value.
 *
 * Accepts either `{currency[, issuer]}` for XRP/IOU or
 * `{mpt_issuance_id}` for MPT. Throws on malformed input.
 *
 * @param jv The JSON object describing the asset.
 * @return The parsed `Asset`.
 * @throws std::runtime_error (or equivalent) if `jv` is not a valid asset.
 */
Asset
assetFromJson(json::Value const& jv);

/**
 * @brief Returns `true` if the asset's internal fields are mutually consistent.
 *
 * For XRP/IOU assets, delegates to `Issue::isConsistent()` which checks that
 * XRP has no account component. MPT assets are always considered consistent.
 * Less strict than `validAsset()` — does not reject sentinel currencies.
 *
 * @param asset The asset to check.
 */
inline bool
isConsistent(Asset const& asset)
{
    return asset.visit(
        [](Issue const& issue) { return isConsistent(issue); },
        [](MPTIssue const&) { return true; });
}

/**
 * @brief Returns `true` if the asset is a well-formed, non-sentinel value.
 *
 * Stricter than `isConsistent()`: additionally rejects `badCurrency()` for
 * IOU/XRP assets and the zero-issuer sentinel for MPT assets. Use this to
 * validate user-provided or deserialized assets before operating on them.
 *
 * @param asset The asset to validate.
 */
inline bool
validAsset(Asset const& asset)
{
    return asset.visit(
        [](Issue const& issue) { return isConsistent(issue) && issue.currency != badCurrency(); },
        [](MPTIssue const& issue) { return issue.getIssuer() != xrpAccount(); });
}

/**
 * @brief Appends an asset's hash contribution to a Hasher.
 *
 * Enables `Asset` as a key in `beast::uhash`-based and `std::unordered_*`
 * containers. Dispatches to the active arm's own `hash_append` specialization,
 * so `Issue` and `MPTIssue` assets produce distinct hash domains.
 *
 * @tparam Hasher A `beast::hash_append`-compatible hasher type.
 * @param h The hasher to accumulate into.
 * @param r The asset to hash.
 */
template <class Hasher>
void
hash_append(Hasher& h, Asset const& r)
{
    using beast::hash_append;
    r.visit(
        [&](Issue const& issue) { hash_append(h, issue); },
        [&](MPTIssue const& issue) { hash_append(h, issue); });
}

/**
 * @brief Stream-inserts a human-readable asset description.
 *
 * Equivalent to `os << to_string(x)`. Intended for logging and diagnostics.
 *
 * @param os The output stream.
 * @param x The asset to write.
 * @return `os`, for chaining.
 */
std::ostream&
operator<<(std::ostream& os, Asset const& x);

}  // namespace xrpl
