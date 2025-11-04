#ifndef JOIN_H_INCLUDED
#define JOIN_H_INCLUDED

#include <string>

namespace ripple {

template <class Stream, class Iter>
Stream&
join(Stream& s, Iter iter, Iter end, std::string const& delimiter)
{
    if (iter == end)
        return s;
    s << *iter;
    for (++iter; iter != end; ++iter)
        s << delimiter << *iter;
    return s;
}

template <class Collection>
class CollectionAndDelimiter
{
public:
    Collection const& collection;
    std::string const delimiter;

    explicit CollectionAndDelimiter(Collection const& c, std::string delim)
        : collection(c), delimiter(std::move(delim))
    {
    }

    template <class Stream>
    friend Stream&
    operator<<(Stream& s, CollectionAndDelimiter const& cd)
    {
        return join(
            s,
            std::begin(cd.collection),
            std::end(cd.collection),
            cd.delimiter);
    }
};

template <class Collection, std::size_t N>
class CollectionAndDelimiter<Collection[N]>
{
public:
    Collection const* collection;
    std::string const delimiter;

    explicit CollectionAndDelimiter(Collection const c[N], std::string delim)
        : collection(c), delimiter(std::move(delim))
    {
    }

    template <class Stream>
    friend Stream&
    operator<<(Stream& s, CollectionAndDelimiter const& cd)
    {
        return join(s, cd.collection, cd.collection + N, cd.delimiter);
    }
};

// Specialization for const char* strings
template <std::size_t N>
class CollectionAndDelimiter<char[N]>
{
public:
    char const* collection;
    std::string const delimiter;

    explicit CollectionAndDelimiter(char const c[N], std::string delim)
        : collection(c), delimiter(std::move(delim))
    {
    }

    template <class Stream>
    friend Stream&
    operator<<(Stream& s, CollectionAndDelimiter const& cd)
    {
        auto end = cd.collection + N;
        if (N > 0 && *(end - 1) == '\0')
            --end;
        return join(s, cd.collection, end, cd.delimiter);
    }
};

}  // namespace ripple

#endif
