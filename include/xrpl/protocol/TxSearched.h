#pragma once

namespace xrpl {

/** Coverage completeness of a transaction search over the node's local ledger history.
 *
 *  When a client queries for a transaction by hash and the transaction is not
 *  found, the result is ambiguous: the transaction may genuinely not exist in
 *  the requested range, or the node may simply lack some of those ledgers
 *  locally. This enum is the discriminant that resolves that ambiguity.
 *
 *  It is the alternative type in the `std::variant` returned by
 *  `TransactionMaster::fetch()` and `RelationalDatabase::getTransaction()`:
 *  when no transaction is found, the variant holds a `TxSearched` value
 *  encoding both the "not found" signal and the confidence level of that
 *  determination.
 *
 *  The RPC layer collapses `All` and `Some` into a boolean `searched_all`
 *  field in the JSON response, and suppresses the field entirely when the
 *  state is `Unknown`.
 *
 *  @see TransactionMaster::fetch()
 *  @see RelationalDatabase::getTransaction()
 */
enum class TxSearched {
    /** The node searched its entire local history for the requested ledger
     *  range and confirmed the transaction is absent. The search was
     *  exhaustive; a client can be confident the transaction was not
     *  included in that range.
     */
    All,

    /** The node attempted the search but its local ledger store is
     *  incomplete for the requested range — some ledgers were missing from
     *  the database. Absence of the transaction is therefore inconclusive.
     */
    Some,

    /** Coverage information is unavailable. This occurs when no ledger
     *  range was specified, a deserialization error occurred, or the
     *  database layer could not determine search coverage. The
     *  `searched_all` field is suppressed in the JSON response when this
     *  state is active.
     */
    Unknown,
};

}  // namespace xrpl
