//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_LRU_MAP_H_INCLUDED
#define RIPPLE_APP_LRU_MAP_H_INCLUDED

#include <list>
#include <map>
#include <utility>

namespace ripple {

template <typename Key, typename Value>
class LRUMap
{
public:
    explicit LRUMap(std::size_t capacity) : capacity_(capacity)
    {
        // TODO: check capacity_ > 0
    }

    Value&
    operator[](Key const& key)
    {
        auto it = data_.find(key);
        if (it != data_.end())
        {
            bump_to_front(key);
            return it->second;
        }

        if (data_.size() >= capacity_)
        {
            std::size_t excess = (data_.size() + 1) - capacity_;
            for (std::size_t i = 0; i < excess; ++i)
            {
                auto lru = usage_list_.back();
                usage_list_.pop_back();
                data_.erase(lru);
            }
        }

        usage_list_.push_front(key);
        return data_[key];
    }

    auto
    find(Key const& key)
    {
        return data_.find(key);
    }
    auto
    find(Key const& key) const
    {
        return data_.find(key);
    }

    auto
    begin()
    {
        return data_.begin();
    }
    auto
    begin() const
    {
        return data_.begin();
    }
    auto
    end()
    {
        return data_.end();
    }
    auto
    end() const
    {
        return data_.end();
    }

    bool
    erase(Key const& key)
    {
        auto it = data_.find(key);
        if (it == data_.end())
            return false;
        for (auto list_it = usage_list_.begin(); list_it != usage_list_.end();
             ++list_it)
        {
            if (*list_it == key)
            {
                usage_list_.erase(list_it);
                break;
            }
        }
        data_.erase(it);
        return true;
    }

    std::size_t
    size() const noexcept
    {
        return data_.size();
    }
    std::size_t
    capacity() const noexcept
    {
        return capacity_;
    }
    void
    clear()
    {
        data_.clear();
        usage_list_.clear();
    }

private:
    void
    bump_to_front(Key const& key)
    {
        for (auto it = usage_list_.begin(); it != usage_list_.end(); ++it)
        {
            if (*it == key)
            {
                usage_list_.erase(it);
                usage_list_.push_front(key);
                return;
            }
        }
    }

    std::size_t capacity_;
    std::map<Key, Value> data_;
    std::list<Key> usage_list_;
};

}  // namespace ripple

#endif