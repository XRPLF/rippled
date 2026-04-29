#include <xrpl/protocol/SecretKey.h>

#include <cstring>

namespace xrpl::test {

/// Compare two SecretKey objects for equality.
/// SecretKey::operator== is deleted, so a named function is used
/// to avoid member-function lookup shadowing free-function overloads.
inline bool
equal(SecretKey const& lhs, SecretKey const& rhs)
{
    return lhs.size() == SecretKey::size_ && rhs.size() == SecretKey::size_ &&
        std::memcmp(lhs.data(), rhs.data(), SecretKey::size_) == 0;
}

}  // namespace xrpl::test
