#ifndef XRPLD_APP_LEDGER_ORDERBOOK_DB_IMPL_H_INCLUDED
#define XRPLD_APP_LEDGER_ORDERBOOK_DB_IMPL_H_INCLUDED

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/AcceptedLedgerTx.h>
#include <xrpl/ledger/BookListeners.h>
#include <xrpl/ledger/OrderBookDB.h>
#include <xrpl/protocol/MultiApiJson.h>
#include <xrpl/protocol/UintTypes.h>

#include <mutex>
#include <optional>

namespace xrpl {

/** Configuration for OrderBookDB */
struct OrderBookDBConfig
{
    int pathSearchMax;
    bool standalone;
};

/** Create an OrderBookDB instance.

    @param registry Service registry for accessing other services
    @param config Configuration parameters
    @return A new OrderBookDB instance
*/
std::unique_ptr<OrderBookDB>
make_OrderBookDB(ServiceRegistry& registry, OrderBookDBConfig const& config);

class OrderBookDBImpl final : public OrderBookDB
{
public:
    OrderBookDBImpl(ServiceRegistry& registry, OrderBookDBConfig const& config);

    // OrderBookDB interface implementation
    void
    setup(std::shared_ptr<ReadView const> const& ledger) override;

    void
    addOrderBook(Book const& book) override;

    std::vector<Book>
    getBooksByTakerPays(
        Issue const& issue,
        std::optional<Domain> const& domain = std::nullopt) override;

    int
    getBookSize(
        Issue const& issue,
        std::optional<Domain> const& domain = std::nullopt) override;

    bool
    isBookToXRP(Issue const& issue, std::optional<Domain> domain = std::nullopt)
        override;

    // OrderBookDBImpl-specific methods
    void
    update(std::shared_ptr<ReadView const> const& ledger);

    // see if this txn effects any orderbook
    void
    processTxn(
        std::shared_ptr<ReadView const> const& ledger,
        AcceptedLedgerTx const& alTx,
        MultiApiJson const& jvObj) override;

    BookListeners::pointer
    getBookListeners(Book const&) override;
    BookListeners::pointer
    makeBookListeners(Book const&) override;

private:
    ServiceRegistry& registry_;
    int const pathSearchMax_;
    bool const standalone_;

    // Maps order books by "issue in" to "issue out":
    hardened_hash_map<Issue, hardened_hash_set<Issue>> allBooks_;

    hardened_hash_map<std::pair<Issue, Domain>, hardened_hash_set<Issue>>
        domainBooks_;

    // does an order book to XRP exist
    hash_set<Issue> xrpBooks_;

    // does an order book to XRP exist
    hash_set<std::pair<Issue, Domain>> xrpDomainBooks_;

    std::recursive_mutex mLock;

    using BookToListenersMap = hash_map<Book, BookListeners::pointer>;

    BookToListenersMap mListeners;

    std::atomic<std::uint32_t> seq_;

    beast::Journal const j_;
};

}  // namespace xrpl

#endif
