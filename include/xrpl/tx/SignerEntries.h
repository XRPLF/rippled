#pragma once

#include <xrpl/basics/Expected.h>        //
#include <xrpl/beast/utility/Journal.h>  // beast::Journal
#include <xrpl/protocol/TER.h>           // temMALFORMED
#include <xrpl/protocol/UintTypes.h>     // AccountID
#include <xrpl/tx/Transactor.h>          // NotTEC

#include <optional>
#include <string_view>

namespace xrpl {

// Forward declarations
class STObject;

/** Non-constructible utility scope for the multi-signature co-signer roster.
 *
 *  A signer list is represented as a `std::vector<SignerEntries::SignerEntry>`.
 *  Entries are produced exclusively via `SignerEntries::deserialize()`, which
 *  extracts them from an `STObject` (either a transaction or a live ledger
 *  entry).  This class cannot be instantiated; it exists only to co-locate
 *  the `SignerEntry` type and the `deserialize()` factory under one name.
 *
 *  @see SignerEntry
 *  @see deserialize
 */
class SignerEntries
{
public:
    explicit SignerEntries() = delete;

    /** A single co-signer record extracted from an `sfSignerEntries` array.
     *
     *  Holds the co-signer's account ID, their vote weight toward the quorum,
     *  and an optional destination tag (`sfWalletLocator`) that supports
     *  phantom accounts — signers that may not yet have an on-ledger account
     *  root.
     *
     *  @note Comparison operators are intentionally defined on `account` alone.
     *      Sorting and duplicate detection in `SignerListSet` and
     *      `Transactor::checkMultiSign()` both rely on account-only ordering:
     *      `std::sort()` uses `operator<=>` to produce the sorted vector that
     *      enables the O(n) linear merge in `checkMultiSign()`, and
     *      `std::adjacent_find()` uses `operator==` to detect duplicate
     *      account IDs (a `temBAD_SIGNER` condition).  Including `weight` or
     *      `tag` in either operator would silently break both checks.
     */
    struct SignerEntry
    {
        AccountID account;      /**< The co-signer's account ID. */
        std::uint16_t weight;   /**< Vote weight contributed toward the quorum. */
        std::optional<uint256> tag; /**< Optional `sfWalletLocator` destination tag. */

        SignerEntry(
            AccountID const& inAccount,
            std::uint16_t inWeight,
            std::optional<uint256> inTag)
            : account(inAccount), weight(inWeight), tag(inTag)
        {
        }

        /** Three-way comparison by `account` only, enabling `std::sort` and
         *  the O(n) merge in `Transactor::checkMultiSign()`.
         */
        friend auto
        operator<=>(SignerEntry const& lhs, SignerEntry const& rhs)
        {
            return lhs.account <=> rhs.account;
        }

        /** Equality test by `account` only, enabling duplicate detection via
         *  `std::adjacent_find` after sorting.
         */
        friend bool
        operator==(SignerEntry const& lhs, SignerEntry const& rhs)
        {
            return lhs.account == rhs.account;
        }
    };

    /** Extract and lightly validate the `sfSignerEntries` array from an STObject.
     *
     *  Works against both an `STTx` (during preflight/preclaim) and an `SLE`
     *  (during `checkMultiSign()` against the on-ledger signer list).  Each
     *  array element must carry the `sfSignerEntry` field name; the function
     *  extracts `sfAccount`, `sfSignerWeight`, and optionally `sfWalletLocator`
     *  per entry.  No business-logic validation (quorum reachability, duplicate
     *  detection, self-reference) is performed here — that belongs to
     *  `SignerListSet::validateQuorumAndSignerEntries()`.
     *
     *  The returned vector is pre-reserved to `STTx::kMAX_MULTI_SIGNERS` to
     *  avoid reallocation during iteration.  Callers typically sort it
     *  immediately after return to enable O(n) duplicate detection and the
     *  linear merge in `checkMultiSign()`.
     *
     *  @param obj       Any `STObject` that carries an `sfSignerEntries` field —
     *      a transaction being preflight-checked or a ledger entry being read
     *      during apply.
     *  @param journal   Journal used to emit `trace`-level diagnostics when a
     *      malformed entry is encountered.
     *  @param annotation  Short label — typically `"transaction"` or `"ledger"`
     *      — prepended to journal messages to identify the data source.
     *  @return On success, a vector of `SignerEntry` values in the order they
     *      appear in the `sfSignerEntries` array.  On failure, a `NotTEC` error
     *      code (typically `temMALFORMED`) that callers should propagate
     *      immediately without dereferencing the value.
     */
    static Expected<std::vector<SignerEntry>, NotTEC>
    deserialize(STObject const& obj, beast::Journal journal, std::string_view annotation);
};

}  // namespace xrpl
