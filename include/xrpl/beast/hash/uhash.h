#pragma once

#include <xrpl/beast/hash/hash_append.h>
#include <xrpl/beast/hash/xxhasher.h>

namespace beast {

// Universal hash function
template <class Hasher = Xxhasher>
struct Uhash
{
    Uhash() = default;

    using ResultType = Hasher::ResultType;

    template <class T>
    ResultType
    operator()(T const& t) const noexcept
    {
        Hasher h;
        hash_append(h, t);
        return static_cast<ResultType>(h);
    }
};

}  // namespace beast
