#ifndef XRPL_BASICS_SHAMAP_HASH_H_INCLUDED
#define XRPL_BASICS_SHAMAP_HASH_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/partitioned_unordered_map.h>

#include <ostream>

namespace ripple {

// A SHAMapHash is the hash of a node in a SHAMap, and also the
// type of the hash of the entire SHAMap.

class SHAMapHash
{
    uint256 hash_;

public:
    SHAMapHash() = default;
    explicit SHAMapHash(uint256 const& hash) : hash_(hash)
    {
    }

    uint256 const&
    as_uint256() const
    {
        return hash_;
    }
    uint256&
    as_uint256()
    {
        return hash_;
    }
    bool
    isZero() const
    {
        return hash_.isZero();
    }
    bool
    isNonZero() const
    {
        return hash_.isNonZero();
    }
    int
    signum() const
    {
        return hash_.signum();
    }
    void
    zero()
    {
        hash_.zero();
    }

    friend bool
    operator==(SHAMapHash const& x, SHAMapHash const& y)
    {
        return x.hash_ == y.hash_;
    }

    friend bool
    operator<(SHAMapHash const& x, SHAMapHash const& y)
    {
        return x.hash_ < y.hash_;
    }

    friend std::ostream&
    operator<<(std::ostream& os, SHAMapHash const& x)
    {
        return os << x.hash_;
    }

    friend std::string
    to_string(SHAMapHash const& x)
    {
        return to_string(x.hash_);
    }

    template <class H>
    friend void
    hash_append(H& h, SHAMapHash const& x)
    {
        hash_append(h, x.hash_);
    }
};

inline bool
operator!=(SHAMapHash const& x, SHAMapHash const& y)
{
    return !(x == y);
}

template <>
inline std::size_t
extract(SHAMapHash const& key)
{
    return *reinterpret_cast<std::size_t const*>(key.as_uint256().data());
}

}  // namespace ripple

#endif  // XRPL_BASICS_SHAMAP_HASH_H_INCLUDED
