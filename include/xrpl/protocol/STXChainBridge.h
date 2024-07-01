//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_STXCHAINBRIDGE_H_INCLUDED
#define RIPPLE_PROTOCOL_STXCHAINBRIDGE_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/STIssue.h>

namespace ripple {

class Serializer;
class STObject;

class STXChainBridge final : public STBase, public CountedObject<STXChainBridge>
{
    STAccount lockingChainDoor_{sfLockingChainDoor};
    STIssue lockingChainIssue_{sfLockingChainIssue};
    STAccount issuingChainDoor_{sfIssuingChainDoor};
    STIssue issuingChainIssue_{sfIssuingChainIssue};

public:
    using value_type = STXChainBridge;

    enum class ChainType { locking, issuing };

    static ChainType
    otherChain(ChainType ct);

    static ChainType
    srcChain(bool wasLockingChainSend);

    static ChainType
    dstChain(bool wasLockingChainSend);

    STXChainBridge();

    explicit STXChainBridge(SField const& name);

    STXChainBridge(STXChainBridge const& rhs) = default;

    STXChainBridge(STObject const& o);

    STXChainBridge(
        AccountID const& srcChainDoor,
        Issue const& srcChainIssue,
        AccountID const& dstChainDoor,
        Issue const& dstChainIssue);

    explicit STXChainBridge(Json::Value const& v);

    explicit STXChainBridge(SField const& name, Json::Value const& v);

    explicit STXChainBridge(SerialIter& sit, SField const& name);

    STXChainBridge&
    operator=(STXChainBridge const& rhs) = default;

    std::string
    getText() const override;

    STObject
    toSTObject() const;

    AccountID const&
    lockingChainDoor() const;

    Issue const&
    lockingChainIssue() const;

    AccountID const&
    issuingChainDoor() const;

    Issue const&
    issuingChainIssue() const;

    AccountID const&
    door(ChainType ct) const;

    Issue const&
    issue(ChainType ct) const;

    SerializedTypeID
    getSType() const override;

    Json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

    value_type const&
    value() const noexcept;

private:
    static std::unique_ptr<STXChainBridge>
    construct(SerialIter&, SField const& name);

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend bool
    operator==(STXChainBridge const& lhs, STXChainBridge const& rhs);

    friend bool
    operator<(STXChainBridge const& lhs, STXChainBridge const& rhs);
};

inline bool
operator==(STXChainBridge const& lhs, STXChainBridge const& rhs)
{
    return std::tie(
               lhs.lockingChainDoor_,
               lhs.lockingChainIssue_,
               lhs.issuingChainDoor_,
               lhs.issuingChainIssue_) ==
        std::tie(
               rhs.lockingChainDoor_,
               rhs.lockingChainIssue_,
               rhs.issuingChainDoor_,
               rhs.issuingChainIssue_);
}

inline bool
operator<(STXChainBridge const& lhs, STXChainBridge const& rhs)
{
    return std::tie(
               lhs.lockingChainDoor_,
               lhs.lockingChainIssue_,
               lhs.issuingChainDoor_,
               lhs.issuingChainIssue_) <
        std::tie(
               rhs.lockingChainDoor_,
               rhs.lockingChainIssue_,
               rhs.issuingChainDoor_,
               rhs.issuingChainIssue_);
}

inline AccountID const&
STXChainBridge::lockingChainDoor() const
{
    return lockingChainDoor_.value();
};

inline Issue const&
STXChainBridge::lockingChainIssue() const
{
    return lockingChainIssue_.value();
};

inline AccountID const&
STXChainBridge::issuingChainDoor() const
{
    return issuingChainDoor_.value();
};

inline Issue const&
STXChainBridge::issuingChainIssue() const
{
    return issuingChainIssue_.value();
};

inline STXChainBridge::value_type const&
STXChainBridge::value() const noexcept
{
    return *this;
}

inline AccountID const&
STXChainBridge::door(ChainType ct) const
{
    if (ct == ChainType::locking)
        return lockingChainDoor();
    return issuingChainDoor();
}

inline Issue const&
STXChainBridge::issue(ChainType ct) const
{
    if (ct == ChainType::locking)
        return lockingChainIssue();
    return issuingChainIssue();
}

inline STXChainBridge::ChainType
STXChainBridge::otherChain(ChainType ct)
{
    if (ct == ChainType::locking)
        return ChainType::issuing;
    return ChainType::locking;
}

inline STXChainBridge::ChainType
STXChainBridge::srcChain(bool wasLockingChainSend)
{
    if (wasLockingChainSend)
        return ChainType::locking;
    return ChainType::issuing;
}

inline STXChainBridge::ChainType
STXChainBridge::dstChain(bool wasLockingChainSend)
{
    if (wasLockingChainSend)
        return ChainType::issuing;
    return ChainType::locking;
}

}  // namespace ripple

#endif
