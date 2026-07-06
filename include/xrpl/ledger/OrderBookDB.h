#pragma once

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/AcceptedLedgerTx.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/UintTypes.h>

#include <memory>
#include <optional>
#include <vector>

namespace xrpl {

/** Tracks order books in the ledger.

    This interface provides access to order book information, including:
    - Which order books exist in the ledger
    - Querying order books by issue
    - Managing order book subscriptions

    The order book database is updated as ledgers are accepted and provides
    efficient lookup of order book information for pathfinding and client
    subscriptions.
*/
class OrderBookDB
{
public:
    virtual ~OrderBookDB() = default;

    /** Initialize or update the order book database with a new ledger.

        This method should be called when a new ledger is accepted to update
        the order book database with the current state of all order books.

        @param ledger The ledger to scan for order books
    */
    virtual void
    setup(std::shared_ptr<ReadView const> const& ledger) = 0;

    /** Add an order book to track.

        @param book The order book to add
    */
    virtual void
    addOrderBook(Book const& book) = 0;

    /** Get all order books that want a specific issue.

        Returns a list of all order books where the taker pays the specified
        issue. This is useful for pathfinding to find all possible next hops
        from a given currency.

        @param asset The asset to search for
        @param domain Optional domain restriction for the order book
        @return Vector of books that want this issue
    */
    virtual std::vector<Book>
    getBooksByTakerPays(Asset const& asset, std::optional<Domain> const& domain = std::nullopt) = 0;

    /** Get the count of order books that want a specific issue.

        @param asset The asset to search for
        @param domain Optional domain restriction for the order book
        @return Number of books that want this issue
    */
    virtual int
    getBookSize(Asset const& asset, std::optional<Domain> const& domain = std::nullopt) = 0;

    /** Check if an order book to XRP exists for the given issue.

        @param asset The asset to check
        @param domain Optional domain restriction for the order book
        @return true if a book from this issue to XRP exists
    */
    virtual bool
    isBookToXRP(Asset const& asset, std::optional<Domain> const& domain = std::nullopt) = 0;
};

/** Extract the set of books affected by a transaction.
 *
 *  Walks the transaction's metadata nodes and collects every order book
 *  whose offers were created, modified, or deleted. Used by NetworkOPs to
 *  fan transaction notifications out to book subscribers.
 *
 *  @param alTx The accepted ledger transaction to inspect.
 *  @param j Journal used to log per-node parsing failures. Inspecting an
 *           offer node can throw if a required field is missing; in that
 *           case the bad node is skipped and a warn-level message is
 *           emitted via @p j. Other affected books in the same transaction
 *           are still returned.
 *  @return The set of books whose offers were created, modified, or
 *          deleted. May be empty for non-offer transactions.
 */
hash_set<Book>
affectedBooks(AcceptedLedgerTx const& alTx, beast::Journal const& j);

}  // namespace xrpl
