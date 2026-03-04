#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OrderBookDBImpl.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/tx/transactors/AMM/AMMUtils.h>

namespace xrpl {

OrderBookDBImpl::OrderBookDBImpl(ServiceRegistry& registry, OrderBookDBConfig const& config)
    : registry_(registry)
    , pathSearchMax_(config.pathSearchMax)
    , standalone_(config.standalone)
    , seq_(0)
    , j_(registry.journal("OrderBookDB"))
{
}

std::unique_ptr<OrderBookDB>
make_OrderBookDB(ServiceRegistry& registry, OrderBookDBConfig const& config)
{
    return std::make_unique<OrderBookDBImpl>(registry, config);
}

void
OrderBookDBImpl::setup(std::shared_ptr<ReadView const> const& ledger)
{
    if (!standalone_ && registry_.getOPs().isNeedNetworkLedger())
    {
        JLOG(j_.warn()) << "Eliding full order book update: no ledger";
        return;
    }

    auto seq = seq_.load();

    if (seq != 0)
    {
        if ((ledger->seq() > seq) && ((ledger->seq() - seq) < 25600))
            return;

        if ((ledger->seq() <= seq) && ((seq - ledger->seq()) < 16))
            return;
    }

    if (seq_.exchange(ledger->seq()) != seq)
        return;

    JLOG(j_.debug()) << "Full order book update: " << seq << " to " << ledger->seq();

    if (pathSearchMax_ != 0)
    {
        if (standalone_)
            update(ledger);
        else
            registry_.getJobQueue().addJob(
                jtUPDATE_PF, "OrderBookUpd" + std::to_string(ledger->seq()), [this, ledger]() {
                    update(ledger);
                });
    }
}

void
OrderBookDBImpl::update(std::shared_ptr<ReadView const> const& ledger)
{
    if (pathSearchMax_ == 0)
        return;  // pathfinding has been disabled

    // A newer full update job is pending
    if (auto const seq = seq_.load(); seq > ledger->seq())
    {
        JLOG(j_.debug()) << "Eliding update for " << ledger->seq()
                         << " because of pending update to later " << seq;
        return;
    }

    decltype(allBooks_) allBooks;
    decltype(xrpBooks_) xrpBooks;
    decltype(domainBooks_) domainBooks;
    decltype(xrpDomainBooks_) xrpDomainBooks;

    allBooks.reserve(allBooks_.size());
    xrpBooks.reserve(xrpBooks_.size());

    JLOG(j_.debug()) << "Beginning update (" << ledger->seq() << ")";

    // walk through the entire ledger looking for orderbook/AMM entries
    int cnt = 0;

    try
    {
        for (auto& sle : ledger->sles)
        {
            if (registry_.isStopping())
            {
                JLOG(j_.info()) << "Update halted because the process is stopping";
                seq_.store(0);
                return;
            }

            if (sle->getType() == ltDIR_NODE && sle->isFieldPresent(sfExchangeRate) &&
                sle->getFieldH256(sfRootIndex) == sle->key())
            {
                Book book;

                book.in.currency = sle->getFieldH160(sfTakerPaysCurrency);
                book.in.account = sle->getFieldH160(sfTakerPaysIssuer);
                book.out.currency = sle->getFieldH160(sfTakerGetsCurrency);
                book.out.account = sle->getFieldH160(sfTakerGetsIssuer);
                book.domain = (*sle)[~sfDomainID];

                if (book.domain)
                    domainBooks[{book.in, *book.domain}].insert(book.out);
                else
                    allBooks[book.in].insert(book.out);

                if (book.domain && isXRP(book.out))
                    xrpDomainBooks.insert({book.in, *book.domain});
                else if (isXRP(book.out))
                    xrpBooks.insert(book.in);

                ++cnt;
            }
            else if (sle->getType() == ltAMM)
            {
                auto const issue1 = (*sle)[sfAsset].get<Issue>();
                auto const issue2 = (*sle)[sfAsset2].get<Issue>();
                auto addBook = [&](Issue const& in, Issue const& out) {
                    allBooks[in].insert(out);

                    if (isXRP(out))
                        xrpBooks.insert(in);

                    ++cnt;
                };
                addBook(issue1, issue2);
                addBook(issue2, issue1);
            }
        }
    }
    catch (SHAMapMissingNode const& mn)
    {
        JLOG(j_.info()) << "Missing node in " << ledger->seq() << " during update: " << mn.what();
        seq_.store(0);
        return;
    }

    JLOG(j_.debug()) << "Update completed (" << ledger->seq() << "): " << cnt << " books found";

    {
        std::lock_guard sl(mLock);
        allBooks_.swap(allBooks);
        xrpBooks_.swap(xrpBooks);
        domainBooks_.swap(domainBooks);
        xrpDomainBooks_.swap(xrpDomainBooks);
    }

    registry_.getLedgerMaster().newOrderBookDB();
}

void
OrderBookDBImpl::addOrderBook(Book const& book)
{
    bool toXRP = isXRP(book.out);

    std::lock_guard sl(mLock);

    if (book.domain)
        domainBooks_[{book.in, *book.domain}].insert(book.out);
    else
        allBooks_[book.in].insert(book.out);

    if (book.domain && toXRP)
        xrpDomainBooks_.insert({book.in, *book.domain});
    else if (toXRP)
        xrpBooks_.insert(book.in);
}

// return list of all orderbooks that want this issuerID and currencyID
std::vector<Book>
OrderBookDBImpl::getBooksByTakerPays(Issue const& issue, std::optional<uint256> const& domain)
{
    std::vector<Book> ret;

    {
        std::lock_guard sl(mLock);

        auto getBooks = [&](auto const& container, auto const& key) {
            if (auto it = container.find(key); it != container.end())
            {
                auto const& books = it->second;
                ret.reserve(books.size());

                for (auto const& gets : books)
                    ret.emplace_back(issue, gets, domain);
            }
        };

        if (!domain)
            getBooks(allBooks_, issue);
        else
            getBooks(domainBooks_, std::make_pair(issue, *domain));
    }

    return ret;
}

int
OrderBookDBImpl::getBookSize(Issue const& issue, std::optional<uint256> const& domain)
{
    std::lock_guard sl(mLock);

    if (!domain)
    {
        if (auto it = allBooks_.find(issue); it != allBooks_.end())
            return static_cast<int>(it->second.size());
    }
    else
    {
        if (auto it = domainBooks_.find({issue, *domain}); it != domainBooks_.end())
            return static_cast<int>(it->second.size());
    }

    return 0;
}

bool
OrderBookDBImpl::isBookToXRP(Issue const& issue, std::optional<Domain> domain)
{
    std::lock_guard sl(mLock);
    if (domain)
        return xrpDomainBooks_.contains({issue, *domain});
    return xrpBooks_.contains(issue);
}

BookListeners::pointer
OrderBookDBImpl::makeBookListeners(Book const& book)
{
    std::lock_guard sl(mLock);
    auto ret = getBookListeners(book);

    if (!ret)
    {
        ret = std::make_shared<BookListeners>();

        mListeners[book] = ret;
        XRPL_ASSERT(
            getBookListeners(book) == ret,
            "xrpl::OrderBookDB::makeBookListeners : result roundtrip "
            "lookup");
    }

    return ret;
}

BookListeners::pointer
OrderBookDBImpl::getBookListeners(Book const& book)
{
    BookListeners::pointer ret;
    std::lock_guard sl(mLock);

    auto it0 = mListeners.find(book);
    if (it0 != mListeners.end())
        ret = it0->second;

    return ret;
}

// Based on the meta, send the meta to the streams that are listening.
// We need to determine which streams a given meta effects.
void
OrderBookDBImpl::processTxn(
    std::shared_ptr<ReadView const> const& ledger,
    AcceptedLedgerTx const& alTx,
    MultiApiJson const& jvObj)
{
    std::lock_guard sl(mLock);

    // For this particular transaction, maintain the set of unique
    // subscriptions that have already published it.  This prevents sending
    // the transaction multiple times if it touches multiple ltOFFER
    // entries for the same book, or if it touches multiple books and a
    // single client has subscribed to those books.
    hash_set<std::uint64_t> havePublished;

    for (auto const& node : alTx.getMeta().getNodes())
    {
        try
        {
            if (node.getFieldU16(sfLedgerEntryType) == ltOFFER)
            {
                auto process = [&, this](SField const& field) {
                    if (auto data = dynamic_cast<STObject const*>(node.peekAtPField(field)); data &&
                        data->isFieldPresent(sfTakerPays) && data->isFieldPresent(sfTakerGets))
                    {
                        auto listeners = getBookListeners(
                            {data->getFieldAmount(sfTakerGets).issue(),
                             data->getFieldAmount(sfTakerPays).issue(),
                             (*data)[~sfDomainID]});
                        if (listeners)
                            listeners->publish(jvObj, havePublished);
                    }
                };

                // We need a field that contains the TakerGets and TakerPays
                // parameters.
                if (node.getFName() == sfModifiedNode)
                    process(sfPreviousFields);
                else if (node.getFName() == sfCreatedNode)
                    process(sfNewFields);
                else if (node.getFName() == sfDeletedNode)
                    process(sfFinalFields);
            }
        }
        catch (std::exception const& ex)
        {
            JLOG(j_.info()) << "processTxn: field not found (" << ex.what() << ")";
        }
    }
}

}  // namespace xrpl
