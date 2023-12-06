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

#include <ripple/protocol/impl/STVar.h>

#include <ripple/basics/contract.h>
#include <ripple/protocol/XChainAttestations.h>
#include <ripple/protocol/impl/STVar.h>
#include <ripple/protocol/st.h>

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
    switch (name.fieldType)
    {
        case STI_NOTPRESENT:
            construct<STBase>(name);
            return;
        case STI_UINT8:
            construct<STUInt8>(sit, name);
            return;
        case STI_UINT16:
            construct<STUInt16>(sit, name);
            return;
        case STI_UINT32:
            construct<STUInt32>(sit, name);
            return;
        case STI_UINT64:
            construct<STUInt64>(sit, name);
            return;
        case STI_AMOUNT:
            construct<STAmount>(sit, name);
            return;
        case STI_UINT128:
            construct<STUInt128>(sit, name);
            return;
        case STI_UINT160:
            construct<STUInt160>(sit, name);
            return;
        case STI_UINT256:
            construct<STUInt256>(sit, name);
            return;
        case STI_VECTOR256:
            construct<STVector256>(sit, name);
            return;
        case STI_VL:
            construct<STBlob>(sit, name);
            return;
        case STI_ACCOUNT:
            construct<STAccount>(sit, name);
            return;
        case STI_PATHSET:
            construct<STPathSet>(sit, name);
            return;
        case STI_OBJECT:
            construct<STObject>(sit, name, depth);
            return;
        case STI_ARRAY:
            construct<STArray>(sit, name, depth);
            return;
        case STI_ISSUE:
            construct<STIssue>(sit, name);
            return;
        case STI_XCHAIN_BRIDGE:
            construct<STXChainBridge>(sit, name);
            return;
        default:
            if (auto it = SField::pluginSTypes.find(name.fieldType);
                it != SField::pluginSTypes.end())
            {
                // TODO: figure out how to handle more complex types that have
                // depth
                construct<STPluginType>(sit, name);
                return;
            }
            else
            {
                Throw<std::runtime_error>("Unknown object type");
            }
    }
}

STVar::STVar(int id, SField const& name)
{
    assert((id == STI_NOTPRESENT) || (id == name.fieldType));
    switch (id)
    {
        case STI_NOTPRESENT:
            construct<STBase>(name);
            return;
        case STI_UINT8:
            construct<STUInt8>(name);
            return;
        case STI_UINT16:
            construct<STUInt16>(name);
            return;
        case STI_UINT32:
            construct<STUInt32>(name);
            return;
        case STI_UINT64:
            construct<STUInt64>(name);
            return;
        case STI_AMOUNT:
            construct<STAmount>(name);
            return;
        case STI_UINT128:
            construct<STUInt128>(name);
            return;
        case STI_UINT160:
            construct<STUInt160>(name);
            return;
        case STI_UINT256:
            construct<STUInt256>(name);
            return;
        case STI_VECTOR256:
            construct<STVector256>(name);
            return;
        case STI_VL:
            construct<STBlob>(name);
            return;
        case STI_ACCOUNT:
            construct<STAccount>(name);
            return;
        case STI_PATHSET:
            construct<STPathSet>(name);
            return;
        case STI_OBJECT:
            construct<STObject>(name);
            return;
        case STI_ARRAY:
            construct<STArray>(name);
            return;
        case STI_ISSUE:
            construct<STIssue>(name);
            return;
        case STI_XCHAIN_BRIDGE:
            construct<STXChainBridge>(name);
            return;
        default:
            if (auto it = SField::pluginSTypes.find(name.fieldType);
                it != SField::pluginSTypes.end())
            {
                construct<STPluginType>(name);
                return;
            }
            else
            {
                Throw<std::runtime_error>("Unknown object type");
            }
    }
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

}  // namespace detail
}  // namespace ripple
