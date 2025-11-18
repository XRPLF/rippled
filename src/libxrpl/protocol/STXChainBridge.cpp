#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>

#include <boost/format/free_funcs.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace ripple {

STXChainBridge::STXChainBridge() : STBase{sfXChainBridge}
{
}

STXChainBridge::STXChainBridge(SField const& name) : STBase{name}
{
}

STXChainBridge::STXChainBridge(
    AccountID const& srcChainDoor,
    Issue const& srcChainIssue,
    AccountID const& dstChainDoor,
    Issue const& dstChainIssue)
    : STBase{sfXChainBridge}
    , lockingChainDoor_{sfLockingChainDoor, srcChainDoor}
    , lockingChainIssue_{sfLockingChainIssue, srcChainIssue}
    , issuingChainDoor_{sfIssuingChainDoor, dstChainDoor}
    , issuingChainIssue_{sfIssuingChainIssue, dstChainIssue}
{
}

STXChainBridge::STXChainBridge(STObject const& o)
    : STBase{sfXChainBridge}
    , lockingChainDoor_{sfLockingChainDoor, o[sfLockingChainDoor]}
    , lockingChainIssue_{sfLockingChainIssue, o[sfLockingChainIssue]}
    , issuingChainDoor_{sfIssuingChainDoor, o[sfIssuingChainDoor]}
    , issuingChainIssue_{sfIssuingChainIssue, o[sfIssuingChainIssue]}
{
}

STXChainBridge::STXChainBridge(Json::Value const& v)
    : STXChainBridge{sfXChainBridge, v}
{
}

STXChainBridge::STXChainBridge(SField const& name, Json::Value const& v)
    : STBase{name}
{
    if (!v.isObject())
    {
        Throw<std::runtime_error>(
            "STXChainBridge can only be specified with a 'object' Json value");
    }

    auto checkExtra = [](Json::Value const& v) {
        static auto const jbridge =
            ripple::STXChainBridge().getJson(ripple::JsonOptions::none);
        for (auto it = v.begin(); it != v.end(); ++it)
        {
            std::string const name = it.memberName();
            if (!jbridge.isMember(name))
            {
                Throw<std::runtime_error>(
                    "STXChainBridge extra field detected: " + name);
            }
        }
        return true;
    };
    checkExtra(v);

    Json::Value const& lockingChainDoorStr = v[jss::LockingChainDoor];
    Json::Value const& lockingChainIssue = v[jss::LockingChainIssue];
    Json::Value const& issuingChainDoorStr = v[jss::IssuingChainDoor];
    Json::Value const& issuingChainIssue = v[jss::IssuingChainIssue];

    if (!lockingChainDoorStr.isString())
    {
        Throw<std::runtime_error>(
            "STXChainBridge LockingChainDoor must be a string Json value");
    }
    if (!issuingChainDoorStr.isString())
    {
        Throw<std::runtime_error>(
            "STXChainBridge IssuingChainDoor must be a string Json value");
    }

    auto const lockingChainDoor =
        parseBase58<AccountID>(lockingChainDoorStr.asString());
    auto const issuingChainDoor =
        parseBase58<AccountID>(issuingChainDoorStr.asString());
    if (!lockingChainDoor)
    {
        Throw<std::runtime_error>(
            "STXChainBridge LockingChainDoor must be a valid account");
    }
    if (!issuingChainDoor)
    {
        Throw<std::runtime_error>(
            "STXChainBridge IssuingChainDoor must be a valid account");
    }

    lockingChainDoor_ = STAccount{sfLockingChainDoor, *lockingChainDoor};
    lockingChainIssue_ =
        STIssue{sfLockingChainIssue, issueFromJson(lockingChainIssue)};
    issuingChainDoor_ = STAccount{sfIssuingChainDoor, *issuingChainDoor};
    issuingChainIssue_ =
        STIssue{sfIssuingChainIssue, issueFromJson(issuingChainIssue)};
}

STXChainBridge::STXChainBridge(SerialIter& sit, SField const& name)
    : STBase{name}
    , lockingChainDoor_{sit, sfLockingChainDoor}
    , lockingChainIssue_{sit, sfLockingChainIssue}
    , issuingChainDoor_{sit, sfIssuingChainDoor}
    , issuingChainIssue_{sit, sfIssuingChainIssue}
{
}

void
STXChainBridge::add(Serializer& s) const
{
    lockingChainDoor_.add(s);
    lockingChainIssue_.add(s);
    issuingChainDoor_.add(s);
    issuingChainIssue_.add(s);
}

Json::Value
STXChainBridge::getJson(JsonOptions jo) const
{
    Json::Value v;
    v[jss::LockingChainDoor] = lockingChainDoor_.getJson(jo);
    v[jss::LockingChainIssue] = lockingChainIssue_.getJson(jo);
    v[jss::IssuingChainDoor] = issuingChainDoor_.getJson(jo);
    v[jss::IssuingChainIssue] = issuingChainIssue_.getJson(jo);
    return v;
}

std::string
STXChainBridge::getText() const
{
    return str(
        boost::format("{ %s = %s, %s = %s, %s = %s, %s = %s }") %
        sfLockingChainDoor.getName() % lockingChainDoor_.getText() %
        sfLockingChainIssue.getName() % lockingChainIssue_.getText() %
        sfIssuingChainDoor.getName() % issuingChainDoor_.getText() %
        sfIssuingChainIssue.getName() % issuingChainIssue_.getText());
}

STObject
STXChainBridge::toSTObject() const
{
    STObject o{sfXChainBridge};
    o[sfLockingChainDoor] = lockingChainDoor_;
    o[sfLockingChainIssue] = lockingChainIssue_;
    o[sfIssuingChainDoor] = issuingChainDoor_;
    o[sfIssuingChainIssue] = issuingChainIssue_;
    return o;
}

SerializedTypeID
STXChainBridge::getSType() const
{
    return STI_XCHAIN_BRIDGE;
}

bool
STXChainBridge::isEquivalent(STBase const& t) const
{
    STXChainBridge const* v = dynamic_cast<STXChainBridge const*>(&t);
    return v && (*v == *this);
}

bool
STXChainBridge::isDefault() const
{
    return lockingChainDoor_.isDefault() && lockingChainIssue_.isDefault() &&
        issuingChainDoor_.isDefault() && issuingChainIssue_.isDefault();
}

std::unique_ptr<STXChainBridge>
STXChainBridge::construct(SerialIter& sit, SField const& name)
{
    return std::make_unique<STXChainBridge>(sit, name);
}

STBase*
STXChainBridge::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STXChainBridge::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}
}  // namespace ripple
