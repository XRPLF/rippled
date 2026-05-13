/** @file
 *  Implementation of NodeObject — the immutable (type, hash, blob) value unit
 *  stored in the XRPL node store. Construction is exclusively through the
 *  `createObject()` factory; see NodeObject.h for the class-level contract.
 */

#include <xrpl/nodestore/NodeObject.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>

#include <memory>
#include <utility>

namespace xrpl {

/** Construct a NodeObject, transferring ownership of @p data.
 *
 *  Only reachable via `createObject()`: the `PrivateAccess` parameter is a
 *  private sentinel type that external callers cannot construct, making this
 *  constructor effectively private while remaining compatible with
 *  `std::make_shared`.
 *
 *  @param type   The category of ledger object being stored.
 *  @param data   Serialized payload; moved into the object — the caller's
 *      variable is left in a valid but unspecified state.
 *  @param hash   256-bit content-address key. Not verified against @p data;
 *      the caller is responsible for correctness.
 *  @param       PrivateAccess sentinel — pass `PrivateAccess{}` from within
 *      the class only.
 */
NodeObject::NodeObject(NodeObjectType type, Blob&& data, uint256 const& hash, PrivateAccess)
    : type_(type), hash_(hash), data_(std::move(data))
{
}

std::shared_ptr<NodeObject>
NodeObject::createObject(NodeObjectType type, Blob&& data, uint256 const& hash)
{
    return std::make_shared<NodeObject>(type, std::move(data), hash, PrivateAccess());
}

NodeObjectType
NodeObject::getType() const
{
    return type_;
}

uint256 const&
NodeObject::getHash() const
{
    return hash_;
}

Blob const&
NodeObject::getData() const
{
    return data_;
}

}  // namespace xrpl
