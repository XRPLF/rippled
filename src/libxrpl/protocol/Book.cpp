#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Issue.h>

#include <ostream>
#include <string>

namespace ripple {

bool
isConsistent(Book const& book)
{
    return isConsistent(book.in) && isConsistent(book.out) &&
        book.in != book.out;
}

std::string
to_string(Book const& book)
{
    return to_string(book.in) + "->" + to_string(book.out);
}

std::ostream&
operator<<(std::ostream& os, Book const& x)
{
    os << to_string(x);
    return os;
}

Book
reversed(Book const& book)
{
    return Book(book.out, book.in, book.domain);
}

}  // namespace ripple
