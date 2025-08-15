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

#ifndef RIPPLE_PATH_IMPL_EITHERAMOUNT_H_INCLUDED
#define RIPPLE_PATH_IMPL_EITHERAMOUNT_H_INCLUDED

#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

struct EitherAmount
{
    std::variant<XRPAmount, IOUAmount, MPTAmount> amount;

    explicit EitherAmount() = default;

    template <StepAmount T>
    explicit EitherAmount(T const& a) : amount(a)
    {
    }

    template <StepAmount T>
    [[nodiscard]] bool
    holds() const
    {
        return std::holds_alternative<T>(amount);
    }

    template <StepAmount T>
    [[nodiscard]] T const&
    get() const
    {
        if (!holds<T>())
            Throw<std::logic_error>(
                "EitherAmount doesn't hold requested amount");
        return std::get<T>(amount);
    }

#ifndef NDEBUG
    friend std::ostream&
    operator<<(std::ostream& stream, EitherAmount const& amt)
    {
        std::visit(
            [&]<StepAmount T>(T const& a) { stream << to_string(a); },
            amt.amount);
        return stream;
    }
#endif
};

template <StepAmount T>
T const&
get(EitherAmount const& amt)
{
    return amt.get<T>();
}

}  // namespace ripple

#endif
