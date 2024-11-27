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

#include <xrpl/protocol/detail/STVar.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/XChainAttestations.h>

namespace ripple {
namespace detail {

defaultObject_t defaultObject;
nonPresentObject_t nonPresentObject;

//------------------------------------------------------------------------------

STVar::~STVar()
{
    destroy();
}

STVar::STVar(STVar const& other)
{
    if (other.p_ != nullptr)
        p_ = other.p_->copy(max_size, &d_);
}

STVar::STVar(STVar&& other)
{
    if (other.on_heap())
    {
        p_ = other.p_;
        other.p_ = nullptr;
    }
    else
    {
        p_ = other.p_->move(max_size, &d_);
    }
}

STVar&
STVar::operator=(STVar const& rhs)
{
    if (&rhs != this)
    {
        destroy();
        if (rhs.p_)
            p_ = rhs.p_->copy(max_size, &d_);
        else
            p_ = nullptr;
    }

    return *this;
}

STVar&
STVar::operator=(STVar&& rhs)
{
    if (&rhs != this)
    {
        destroy();
        if (rhs.on_heap())
        {
            p_ = rhs.p_;
            rhs.p_ = nullptr;
        }
        else
        {
            p_ = rhs.p_->move(max_size, &d_);
        }
    }

    return *this;
}

STVar::STVar(defaultObject_t, SField const& name) : STVar(name.fieldType, name)
{
}

STVar::STVar(nonPresentObject_t, SField const& name)
    : STVar(STI_NOTPRESENT, name)
{
}

STVar::STVar(SerialIter& sit, SField const& name, int depth)
{
    if (depth > 10)
        Throw<std::runtime_error>("Maximum nesting depth of STVar exceeded");
    constructST(name.fieldType, depth, sit, name);
}

STVar::STVar(SerializedTypeID id, SField const& name)
{
    assert((id == STI_NOTPRESENT) || (id == name.fieldType));
    constructST(id, 0, name);
}

void
STVar::destroy()
{
    if (on_heap())
        delete p_;
    else
        p_->~STBase();

    p_ = nullptr;
}

template <typename... Args>
    requires ValidConstructSTArgs<Args...>
void
STVar::constructST(SerializedTypeID id, int depth, Args&&... args)
{
    auto constructWithDepth = [&]<typename T>() {
        if constexpr (std::is_same_v<
                          std::tuple<std::remove_cvref_t<Args>...>,
                          std::tuple<SField>>)
        {
            construct<T>(std::forward<Args>(args)...);
        }
        else if constexpr (std::is_same_v<
                               std::tuple<std::remove_cvref_t<Args>...>,
                               std::tuple<SerialIter, SField>>)
        {
            construct<T>(std::forward<Args>(args)..., depth);
        }
        else
        {
            constexpr bool alwaysFalse =
                !std::is_same_v<std::tuple<Args...>, std::tuple<Args...>>;
            static_assert(alwaysFalse, "Invalid STVar constructor arguments");
        }
    };

    switch (id)
    {
        case STI_NOTPRESENT: {
            // Last argument is always SField
            SField const& field =
                std::get<sizeof...(args) - 1>(std::forward_as_tuple(args...));
            construct<STBase>(field);
            return;
        }
        case STI_UINT8:
            construct<STUInt8>(std::forward<Args>(args)...);
            return;
        case STI_UINT16:
            construct<STUInt16>(std::forward<Args>(args)...);
            return;
        case STI_UINT32:
            construct<STUInt32>(std::forward<Args>(args)...);
            return;
        case STI_UINT64:
            construct<STUInt64>(std::forward<Args>(args)...);
            return;
        case STI_AMOUNT:
            construct<STAmount>(std::forward<Args>(args)...);
            return;
        case STI_UINT128:
            construct<STUInt128>(std::forward<Args>(args)...);
            return;
        case STI_UINT160:
            construct<STUInt160>(std::forward<Args>(args)...);
            return;
        case STI_UINT192:
            construct<STUInt192>(std::forward<Args>(args)...);
            return;
        case STI_UINT256:
            construct<STUInt256>(std::forward<Args>(args)...);
            return;
        case STI_VECTOR256:
            construct<STVector256>(std::forward<Args>(args)...);
            return;
        case STI_VL:
            construct<STBlob>(std::forward<Args>(args)...);
            return;
        case STI_ACCOUNT:
            construct<STAccount>(std::forward<Args>(args)...);
            return;
        case STI_PATHSET:
            construct<STPathSet>(std::forward<Args>(args)...);
            return;
        case STI_OBJECT:
            constructWithDepth.template operator()<STObject>();
            return;
        case STI_ARRAY:
            constructWithDepth.template operator()<STArray>();
            return;
        case STI_ISSUE:
            construct<STIssue>(std::forward<Args>(args)...);
            return;
        case STI_XCHAIN_BRIDGE:
            construct<STXChainBridge>(std::forward<Args>(args)...);
            return;
        case STI_CURRENCY:
            construct<STCurrency>(std::forward<Args>(args)...);
            return;
        default:
            Throw<std::runtime_error>("Unknown object type");
    }
}

}  // namespace detail
}  // namespace ripple
