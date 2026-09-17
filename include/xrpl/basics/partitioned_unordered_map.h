#pragma once

#include <xrpl/beast/hash/uhash.h>

#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl {

template <typename Key>
static std::size_t
extract(Key const& key)
{
    return key;
}

template <>
inline std::size_t
extract(std::string const& key)
{
    return ::beast::Uhash<>{}(key);
}

template <
    typename Key,
    typename Value,
    typename Hash,
    typename Pred = std::equal_to<Key>,
    typename Alloc = std::allocator<std::pair<Key const, Value>>>
class PartitionedUnorderedMap
{
    /**
     * How many sub-maps the keys are spread over. Never zero.
     */
    std::size_t partitions_;

public:
    /**
     * Partition count used when the caller does not ask for one.
     *
     * A partition is the unit of parallel work for callers that walk the whole
     * map: TaggedCache::sweep() runs one thread per partition. Holding this
     * small and fixed keeps that thread count independent of how many cores
     * the host has.
     */
    static constexpr std::size_t kDefaultPartitions = 2;

    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key const, mapped_type>;
    using size_type = std::size_t;
    using difference_type = std::size_t;
    using hasher = Hash;
    using key_equal = Pred;
    using allocator_type = Alloc;
    using reference = value_type&;
    using const_reference = value_type const&;
    using pointer = value_type*;
    using const_pointer = value_type const*;
    using map_type = std::unordered_map<key_type, mapped_type, hasher, key_equal, allocator_type>;
    using partition_map_type = std::vector<map_type>;

    struct Iterator
    {
        using iterator_category = std::forward_iterator_tag;
        partition_map_type* map{nullptr};
        partition_map_type::iterator ait{};
        map_type::iterator mit;

        Iterator() = default;

        Iterator(partition_map_type* m) : map(m)
        {
        }

        reference
        operator*() const
        {
            return *mit;
        }

        pointer
        operator->() const
        {
            return &(*mit);
        }

        void
        inc()
        {
            ++mit;
            while (mit == ait->end())
            {
                ++ait;
                if (ait == map->end())
                    return;
                mit = ait->begin();
            }
        }

        // ++it
        Iterator&
        operator++()
        {
            inc();
            return *this;
        }

        // it++
        Iterator
        operator++(int)
        {
            Iterator tmp(*this);
            inc();
            return tmp;
        }

        friend bool
        operator==(Iterator const& lhs, Iterator const& rhs)
        {
            return lhs.map == rhs.map && lhs.ait == rhs.ait && lhs.mit == rhs.mit;
        }
    };

    struct ConstIterator
    {
        using iterator_category = std::forward_iterator_tag;

        partition_map_type* map{nullptr};
        partition_map_type::iterator ait{};
        map_type::iterator mit;

        ConstIterator() = default;

        ConstIterator(partition_map_type* m) : map(m)
        {
        }

        ConstIterator(Iterator const& orig) : map(orig.map), ait(orig.ait), mit(orig.mit)
        {
        }

        const_reference
        operator*() const
        {
            return *mit;
        }

        const_pointer
        operator->() const
        {
            return &(*mit);
        }

        void
        inc()
        {
            ++mit;
            while (mit == ait->end())
            {
                ++ait;
                if (ait == map->end())
                    return;
                mit = ait->begin();
            }
        }

        // ++it
        ConstIterator&
        operator++()
        {
            inc();
            return *this;
        }

        // it++
        ConstIterator
        operator++(int)
        {
            ConstIterator tmp(*this);
            inc();
            return tmp;
        }

        friend bool
        operator==(ConstIterator const& lhs, ConstIterator const& rhs)
        {
            return lhs.map == rhs.map && lhs.ait == rhs.ait && lhs.mit == rhs.mit;
        }
    };

private:
    std::size_t
    partitioner(Key const& key) const
    {
        return extract(key) % partitions_;
    }

    template <class T>
    static void
    end(T& it)
    {
        it.ait = it.map->end();
        it.mit = it.map->back().end();
    }

    template <class T>
    static void
    begin(T& it)
    {
        for (it.ait = it.map->begin(); it.ait != it.map->end(); ++it.ait)
        {
            if (it.ait->begin() == it.ait->end())
                continue;
            it.mit = it.ait->begin();
            return;
        }
        end(it);
    }

public:
    /**
     * Builds an empty map spread over a fixed number of partitions.
     *
     * @param partitions How many partitions to use. An empty optional, or 0,
     * selects kDefaultPartitions.
     */
    PartitionedUnorderedMap(std::optional<std::size_t> partitions = std::nullopt)
        : partitions_(partitions && (*partitions != 0u) ? *partitions : kDefaultPartitions)
    {
        map_.resize(partitions_);
    }

    /**
     * Returns how many partitions the keys are spread over.
     */
    std::size_t
    partitions() const
    {
        return partitions_;
    }

    partition_map_type&
    map()
    {
        return map_;
    }

    Iterator
    begin()
    {
        Iterator it(&map_);
        begin(it);
        return it;
    }

    ConstIterator
    cbegin() const
    {
        ConstIterator it(&map_);
        begin(it);
        return it;
    }

    ConstIterator
    begin() const
    {
        return cbegin();
    }

    Iterator
    end()
    {
        Iterator it(&map_);
        end(it);
        return it;
    }

    ConstIterator
    cend() const
    {
        ConstIterator it(&map_);
        end(it);
        return it;
    }

    ConstIterator
    end() const
    {
        return cend();
    }

private:
    template <class T>
    void
    find(key_type const& key, T& it) const
    {
        it.ait = it.map->begin() + partitioner(key);
        it.mit = it.ait->find(key);
        if (it.mit == it.ait->end())
            end(it);
    }

public:
    Iterator
    find(key_type const& key)
    {
        Iterator it(&map_);
        find(key, it);
        return it;
    }

    ConstIterator
    find(key_type const& key) const
    {
        ConstIterator it(&map_);
        find(key, it);
        return it;
    }

    template <class T, class U>
    std::pair<Iterator, bool>
    emplace(std::piecewise_construct_t const&, T&& keyTuple, U&& valueTuple)
    {
        auto const& key = std::get<0>(keyTuple);
        Iterator it(&map_);
        it.ait = it.map->begin() + partitioner(key);
        auto [eit, inserted] = it.ait->emplace(
            std::piecewise_construct, std::forward<T>(keyTuple), std::forward<U>(valueTuple));
        it.mit = eit;
        return {it, inserted};
    }

    template <class T, class U>
    std::pair<Iterator, bool>
    emplace(T&& key, U&& val)
    {
        Iterator it(&map_);
        it.ait = it.map->begin() + partitioner(key);
        auto [eit, inserted] = it.ait->emplace(std::forward<T>(key), std::forward<U>(val));
        it.mit = eit;
        return {it, inserted};
    }

    void
    clear()
    {
        for (auto& p : map_)
            p.clear();
    }

    Iterator
    erase(ConstIterator position)
    {
        Iterator it(&map_);
        it.ait = position.ait;
        it.mit = position.ait->erase(position.mit);

        while (it.mit == it.ait->end())
        {
            ++it.ait;
            if (it.ait == it.map->end())
                break;
            it.mit = it.ait->begin();
        }

        return it;
    }

    std::size_t
    size() const
    {
        std::size_t ret = 0;
        for (auto& p : map_)
            ret += p.size();
        return ret;
    }

    Value&
    operator[](Key const& key)
    {
        return map_[partitioner(key)][key];
    }

private:
    mutable partition_map_type map_{};
};

}  // namespace xrpl
