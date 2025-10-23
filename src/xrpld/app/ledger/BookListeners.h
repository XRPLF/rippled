#ifndef XRPL_APP_LEDGER_BOOKLISTENERS_H_INCLUDED
#define XRPL_APP_LEDGER_BOOKLISTENERS_H_INCLUDED

#include <xrpld/rpc/InfoSub.h>

#include <xrpl/protocol/MultiApiJson.h>

#include <memory>
#include <mutex>

namespace ripple {

/** Listen to public/subscribe messages from a book. */
class BookListeners
{
public:
    using pointer = std::shared_ptr<BookListeners>;

    BookListeners()
    {
    }

    /** Add a new subscription for this book
     */
    void
    addSubscriber(InfoSub::ref sub);

    /** Stop publishing to a subscriber
     */
    void
    removeSubscriber(std::uint64_t sub);

    /** Publish a transaction to subscribers

        Publish a transaction to clients subscribed to changes on this book.
        Uses havePublished to prevent sending duplicate transactions to clients
        that have subscribed to multiple books.

        @param jvObj JSON transaction data to publish
        @param havePublished InfoSub sequence numbers that have already
                             published this transaction.

    */
    void
    publish(MultiApiJson const& jvObj, hash_set<std::uint64_t>& havePublished);

private:
    std::recursive_mutex mLock;

    hash_map<std::uint64_t, InfoSub::wptr> mListeners;
};

}  // namespace ripple

#endif
