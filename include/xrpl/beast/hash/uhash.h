#ifndef BEAST_HASH_UHASH_H_INCLUDED
#define BEAST_HASH_UHASH_H_INCLUDED

#include <xrpl/beast/hash/hash_append.h>
#include <xrpl/beast/hash/xxhasher.h>

namespace beast {

// Universal hash function
template <class Hasher = xxhasher>
struct uhash
{
    uhash() = default;

    using result_type = typename Hasher::result_type;

    template <class T>
    result_type
    operator()(T const& t) const noexcept
    {
        Hasher h;
        hash_append(h, t);
        return static_cast<result_type>(h);
    }
};

}  // namespace beast

#endif
