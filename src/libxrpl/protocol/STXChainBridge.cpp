/** @file
 *  Implements STXChainBridge: the serialized type encoding a cross-chain
 *  bridge specification (locking-chain door + asset, issuing-chain door +
 *  wrapped asset) as a first-class STBase-derived wire-format field.
 *
 *  Construction paths range from trusted internal use (no validation) through
 *  binary deserialization (SerialIter) to the fully defensive JSON path, which
 *  rejects unknown fields via a key-set comparison against a default-constructed
 *  instance before parsing any values.
 *
 *  The static @c construct() factory is the hook registered in STVar.cpp's
 *  @c constructST() switch under @c STI_XCHAIN_BRIDGE, allowing the generic
 *  binary deserializer to materialize this type by type-ID alone.
 */
#include <xrpl/protocol/STXChainBridge.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/jss.h>

#include <boost/format/free_funcs.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

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

STXChainBridge::STXChainBridge(json::Value const& v) : STXChainBridge{sfXChainBridge, v}
{
}

STXChainBridge::STXChainBridge(SField const& name, json::Value const& v) : STBase{name}
{
    if (!v.isObject())
    {
        Throw<std::runtime_error>(
            "STXChainBridge can only be specified with a 'object' Json value");
    }

    // Guard against unknown or misspelled field names by comparing the input
    // key set against the canonical JSON produced by a default-constructed
    // bridge.  The reference set is computed once (static) and reused on every
    // call.  Any key absent from that set is rejected immediately, preventing
    // silent discard of typo'd or future fields.
    auto checkExtra = [](json::Value const& v) {
        static auto const kBRIDGE_JSON =
            xrpl::STXChainBridge().getJson(xrpl::JsonOptions::Values::None);
        for (auto it = v.begin(); it != v.end(); ++it)
        {
            std::string const name = it.memberName();
            if (!kBRIDGE_JSON.isMember(name))
            {
                Throw<std::runtime_error>("STXChainBridge extra field detected: " + name);
            }
        }
        return true;
    };
    checkExtra(v);

    json::Value const& lockingChainDoorStr = v[jss::LockingChainDoor];
    json::Value const& lockingChainIssue = v[jss::LockingChainIssue];
    json::Value const& issuingChainDoorStr = v[jss::IssuingChainDoor];
    json::Value const& issuingChainIssue = v[jss::IssuingChainIssue];

    if (!lockingChainDoorStr.isString())
    {
        Throw<std::runtime_error>("STXChainBridge LockingChainDoor must be a string Json value");
    }
    if (!issuingChainDoorStr.isString())
    {
        Throw<std::runtime_error>("STXChainBridge IssuingChainDoor must be a string Json value");
    }

    auto const lockingChainDoor = parseBase58<AccountID>(lockingChainDoorStr.asString());
    auto const issuingChainDoor = parseBase58<AccountID>(issuingChainDoorStr.asString());
    if (!lockingChainDoor)
    {
        Throw<std::runtime_error>("STXChainBridge LockingChainDoor must be a valid account");
    }
    if (!issuingChainDoor)
    {
        Throw<std::runtime_error>("STXChainBridge IssuingChainDoor must be a valid account");
    }

    lockingChainDoor_ = STAccount{sfLockingChainDoor, *lockingChainDoor};
    lockingChainIssue_ = STIssue{sfLockingChainIssue, issueFromJson(lockingChainIssue)};
    issuingChainDoor_ = STAccount{sfIssuingChainDoor, *issuingChainDoor};
    issuingChainIssue_ = STIssue{sfIssuingChainIssue, issueFromJson(issuingChainIssue)};
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

json::Value
STXChainBridge::getJson(JsonOptions jo) const
{
    json::Value v;
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
        boost::format("{ %s = %s, %s = %s, %s = %s, %s = %s }") % sfLockingChainDoor.getName() %
        lockingChainDoor_.getText() % sfLockingChainIssue.getName() % lockingChainIssue_.getText() %
        sfIssuingChainDoor.getName() % issuingChainDoor_.getText() % sfIssuingChainIssue.getName() %
        issuingChainIssue_.getText());
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
    return (v != nullptr) && (*v == *this);
}

bool
STXChainBridge::isDefault() const
{
    return lockingChainDoor_.isDefault() && lockingChainIssue_.isDefault() &&
        issuingChainDoor_.isDefault() && issuingChainIssue_.isDefault();
}

/** Factory called by the STVar dispatch table (STVar.cpp, case STI_XCHAIN_BRIDGE)
 *  to materialize an STXChainBridge from a binary stream by type-ID alone.
 *
 *  @param sit  Forward-only cursor over the incoming byte stream; consumed
 *      in declaration order: locking door, locking issue, issuing door,
 *      issuing issue.
 *  @param name The SField tag to associate with the constructed object.
 *  @return Heap-allocated STXChainBridge ready for ownership transfer into
 *      the enclosing STVar or STObject.
 */
std::unique_ptr<STXChainBridge>
STXChainBridge::construct(SerialIter& sit, SField const& name)
{
    return std::make_unique<STXChainBridge>(sit, name);
}

/** Satisfy the STBase small-buffer-optimization interface for copy construction.
 *
 *  STVar maintains an aligned inline buffer; if sizeof(*this) fits within @p n
 *  bytes the object is placement-new'd there, otherwise it is heap-allocated.
 *  @c emplace() (inherited from STBase) encapsulates that decision.
 *
 *  @param n   Size of the inline buffer offered by the enclosing STVar.
 *  @param buf Pointer to that inline buffer (may be ignored if too small).
 *  @return Pointer to the newly constructed copy, either at @p buf or on the heap.
 */
STBase*
STXChainBridge::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

/** Satisfy the STBase small-buffer-optimization interface for move construction.
 *
 *  Identical contract to @c copy(), but invokes the move constructor, allowing
 *  STVar to steal heap-allocated members when the object does not fit inline.
 *
 *  @param n   Size of the inline buffer offered by the enclosing STVar.
 *  @param buf Pointer to that inline buffer (may be ignored if too small).
 *  @return Pointer to the newly constructed object, either at @p buf or on the heap.
 */
STBase*
STXChainBridge::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}
}  // namespace xrpl
