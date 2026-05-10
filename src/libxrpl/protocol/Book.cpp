#include <xrpl/protocol/Book.h>

#include <xrpl/protocol/Asset.h>
#include <xrpl/basics/TraceLog.h>

#include <ostream>
#include <string>

namespace xrpl {

bool
isConsistent(Book const& book)
{
    TRACE_FUNC();
    return isConsistent(book.in) && isConsistent(book.out) && book.in != book.out;
}

std::string
to_string(Book const& book)
{
    TRACE_FUNC();
    return to_string(book.in) + "->" + to_string(book.out);
}

std::ostream&
operator<<(std::ostream& os, Book const& x)
{
    TRACE_FUNC();
    os << to_string(x);
    return os;
}

Book
reversed(Book const& book)
{
    TRACE_FUNC();
    return Book(book.out, book.in, book.domain);
}

}  // namespace xrpl
