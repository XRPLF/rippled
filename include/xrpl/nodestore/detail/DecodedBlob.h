#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/nodestore/NodeObject.h>

#include <memory>
#include <span>

namespace xrpl::node_store {

/**
 * Parsed key/value blob into NodeObject components.
 *
 * This will extract the information required to construct a NodeObject. It
 * also does consistency checking and returns the result, so it is possible
 * to determine if the data is corrupted without throwing an exception. Not
 * all forms of corruption are detected so further analysis will be needed
 * to eliminate false negatives.
 *
 * @note This defines the database format of a NodeObject!
 */
class DecodedBlob
{
public:
    /**
     * Construct the decoded blob from raw data.
     */
    DecodedBlob(uint256 const& key, void const* value, int valueBytes);

    /**
     * Transitional: construct from a raw key pointer.
     * The buffer must be at least 32 bytes; this is not checked.
     */
    DecodedBlob(void const* key, void const* value, int valueBytes)
        : DecodedBlob(
              uint256{std::span<unsigned char const, uint256::size()>{
                  static_cast<unsigned char const*>(key), uint256::size()}},
              value,
              valueBytes)
    {
    }

    /**
     * Determine if the decoding was successful.
     */
    [[nodiscard]] bool
    wasOk() const noexcept
    {
        return success_;
    }

    /**
     * Create a NodeObject from this data.
     */
    std::shared_ptr<NodeObject>
    createObject();

private:
    bool success_{false};

    uint256 key_;
    NodeObjectType objectType_{NodeObjectType::Unknown};
    unsigned char const* objectData_{nullptr};
    int dataBytes_;
};

}  // namespace xrpl::node_store
