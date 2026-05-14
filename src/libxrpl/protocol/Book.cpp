#include <xrpl/protocol/Book.h>

#include <xrpl/protocol/Asset.h>

#include <ostream>
#include <string>

namespace xrpl {

/** Validate that a Book is self-consistent.
 *
 *  Composes `isConsistent` across both legs and adds the invariant that the
 *  two legs must differ. A book is inconsistent if either `in` or `out` is
 *  internally inconsistent (e.g. an XRP currency paired with a non-XRP
 *  issuer), or if both legs name the same asset — which would represent
 *  trading a currency against itself.
 *
 *  @note `book.domain` is not validated here; semantic validity of the
 *      domain identifier belongs to higher-level transaction processing.
 *  @note Returns `false` rather than throwing, matching the soft-validation
 *      convention used throughout the protocol layer. Callers decide whether
 *      inconsistency is fatal. In practice this is used as a hard guard in
 *      the Subscribe RPC handler and as an assertion in `getBookBase()`.
 *  @param book The order book to validate.
 *  @return `true` if both legs are individually consistent and `in != out`.
 */
bool
isConsistent(Book const& book)
{
    return isConsistent(book.in) && isConsistent(book.out) && book.in != book.out;
}

/** Produce a diagnostic string representation of a Book.
 *
 *  Formats the book as `<in>-><out>` using the `to_string` representations
 *  of each leg. The arrow makes directionality explicit, reflecting that
 *  order books are one-way markets. Intended for logging and debugging only;
 *  this format is not part of the wire protocol.
 *
 *  @param book The order book to convert.
 *  @return A string of the form `"<in>-><out>"`.
 */
std::string
to_string(Book const& book)
{
    return to_string(book.in) + "->" + to_string(book.out);
}

/** Write a Book to an output stream in diagnostic form.
 *
 *  Delegates to `to_string(book)`, keeping a single formatting path.
 *
 *  @param os The output stream to write to.
 *  @param x The order book to write.
 *  @return `os`, to allow chaining.
 */
std::ostream&
operator<<(std::ostream& os, Book const& x)
{
    os << to_string(x);
    return os;
}

/** Return the mirror-image order book with `in` and `out` swapped.
 *
 *  Swaps the two asset legs while preserving `domain` unchanged — a
 *  domain-scoped market is the same market when viewed from either direction.
 *  This is a pure function; the original book is not modified.
 *
 *  Used by the Subscribe/Unsubscribe RPC handlers when a client requests the
 *  `both` flag, so updates from both the bid and ask sides of the same market
 *  are delivered, and by `BookDirs` tests to traverse offer directories from
 *  the opposite direction.
 *
 *  @param book The order book to reverse.
 *  @return A new `Book` with `in` and `out` exchanged and `domain` unchanged.
 */
Book
reversed(Book const& book)
{
    return Book(book.out, book.in, book.domain);
}

}  // namespace xrpl
