#pragma once

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/OrderBookDB.h>
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
makeOrderBookDb(ServiceRegistry& registry, OrderBookDBConfig const& config);

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
    getBooksByTakerPays(Asset const& asset, std::optional<Domain> const& domain = std::nullopt)
        override;

    int
    getBookSize(Asset const& asset, std::optional<Domain> const& domain = std::nullopt) override;

    bool
    isBookToXRP(Asset const& asset, std::optional<Domain> const& domain = std::nullopt) override;

    // OrderBookDBImpl-specific methods
    void
    update(std::shared_ptr<ReadView const> const& ledger);

private:
    std::reference_wrapper<ServiceRegistry> registry_;
    int const pathSearchMax_;
    bool const standalone_;

    // Maps order books by "asset in" to "asset out":
    hardened_hash_map<Asset, hardened_hash_set<Asset>> allBooks_;

    hardened_hash_map<std::pair<Asset, Domain>, hardened_hash_set<Asset>> domainBooks_;

    // does an order book to XRP exist
    hash_set<Asset> xrpBooks_;

    // does an order book to XRP exist
    hash_set<std::pair<Asset, Domain>> xrpDomainBooks_;

    std::recursive_mutex lock_;

    std::atomic<std::uint32_t> seq_;

    beast::Journal const j_;
};

}  // namespace xrpl
