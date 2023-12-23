//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_ASSET_H_INCLUDED
#define RIPPLE_PROTOCOL_ASSET_H_INCLUDED

#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/UintTypes.h>

#include <variant>

namespace ripple {

class Asset
{
    inline static CFT none = noCFT();
    using asset_type = std::variant<Currency, CFT>;

private:
    asset_type asset_;

public:
    Asset() : asset_(Currency{beast::zero})
    {
    }
    Asset(Currency const& c) : asset_(c)
    {
    }
    Asset(CFT const& u) : asset_(u)
    {
    }
    Asset&
    operator=(Currency const& c)
    {
        asset_ = c;
        return *this;
    }
    Asset&
    operator=(CFT const& u)
    {
        asset_ = u;
        return *this;
    }

    asset_type const&
    asset() const
    {
        return asset_;
    }

    bool
    isCFT() const
    {
        return std::holds_alternative<CFT>(asset_);
    }
    bool
    isCurrency() const
    {
        return std::holds_alternative<Currency>(asset_);
    }
    bool
    isXRP() const
    {
        return isCurrency() && ripple::isXRP(std::get<Currency>(asset_));
    }

    void
    addBitString(Serializer& s) const
    {
        if (isCurrency())
            s.addBitString(std::get<Currency>(asset_));
        else
        {
            s.add32(std::get<CFT>(asset_).first);
            s.addBitString(std::get<CFT>(asset_).second);
        }
    }

    bool
    empty() const
    {
        return std::holds_alternative<CFT>(asset_) &&
            std::get<CFT>(asset_) == none;
    }

    template <typename Hasher>
    friend void
    hash_append(Hasher& h, Asset const& a)
    {
        std::visit([&h](auto&& arg) { hash_append(h, arg); }, a.asset_);
    }

    template <typename T>
    requires(std::is_same_v<T, Currency> || std::is_same_v<T, CFT>)
        T const* get() const
    {
        return std::get_if<T>(asset_);
    }

    template <typename T>
    requires(std::is_same_v<T, Currency> || std::is_same_v<T, CFT>) operator T
        const &() const
    {
        assert(std::holds_alternative<T>(asset_));
        if (!std::holds_alternative<T>(asset_))
            Throw<std::logic_error>("Invalid Asset cast");
        return std::get<T>(asset_);
    }

    friend bool
    comparable(Asset const& a1, Asset const& a2)
    {
        return std::holds_alternative<Currency>(a1.asset_) ==
            std::holds_alternative<Currency>(a2.asset_);
    }
    friend bool
    operator==(Currency const& c, Asset const& a) noexcept
    {
        return a.isCurrency() && c == (Currency&)a;
    }
    friend bool
    operator==(Asset const& a, Currency const& c) noexcept
    {
        return c == a;
    }
    friend bool
    operator==(Asset const& a1, Asset const& a2) noexcept
    {
        return comparable(a1, a2) && a1.asset_ == a2.asset_;
    }
    friend bool
    operator!=(Asset const& a1, Asset const& a2) noexcept
    {
        return !(a1 == a2);
    }
    friend bool
    operator<(Asset const& a1, Asset const& a2) noexcept
    {
        return comparable(a1, a2) && a1.asset_ < a2.asset_;
    }
    friend bool
    operator>(Asset const& a1, Asset const& a2) noexcept
    {
        return a2 < a1;
    }
    friend bool
    operator<=(Asset const& a1, Asset const& a2) noexcept
    {
        return !(a2 < a1);
    }
    friend bool
    operator>=(Asset const& a1, Asset const& a2) noexcept
    {
        return !(a1 < a2);
    }
    friend std::string
    to_string(Asset const& a)
    {
        if (a.isCurrency())
            return to_string((Currency&)a);
        // TODO, common getCftID()
        uint192 u;
        auto const sequence =
            boost::endian::native_to_big(std::get<CFT>(a.asset_).first);
        auto const& account = std::get<CFT>(a.asset_).second;
        memcpy(u.data(), &sequence, sizeof(sequence));
        memcpy(u.data() + sizeof(sequence), account.data(), sizeof(account));
        return to_string(u);
    }
    friend bool
    isXRP(Asset const& a)
    {
        return !a.empty() && a.isXRP();
    }
    friend std::ostream&
    operator<<(std::ostream& stream, Asset const& a)
    {
        stream << to_string(a);
        return stream;
    }
};

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_ASSET_H_INCLUDED
