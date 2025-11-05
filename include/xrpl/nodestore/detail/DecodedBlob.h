#ifndef XRPL_NODESTORE_DECODEDBLOB_H_INCLUDED
#define XRPL_NODESTORE_DECODEDBLOB_H_INCLUDED

#include <xrpl/nodestore/NodeObject.h>

namespace ripple {
namespace NodeStore {

/** Parsed key/value blob into NodeObject components.

    This will extract the information required to construct a NodeObject. It
    also does consistency checking and returns the result, so it is possible
    to determine if the data is corrupted without throwing an exception. Not
    all forms of corruption are detected so further analysis will be needed
    to eliminate false negatives.

    @note This defines the database format of a NodeObject!
*/
class DecodedBlob
{
public:
    /** Construct the decoded blob from raw data. */
    DecodedBlob(void const* key, void const* value, int valueBytes);

    /** Determine if the decoding was successful. */
    bool
    wasOk() const noexcept
    {
        return m_success;
    }

    /** Create a NodeObject from this data. */
    std::shared_ptr<NodeObject>
    createObject();

private:
    bool m_success;

    void const* m_key;
    NodeObjectType m_objectType;
    unsigned char const* m_objectData;
    int m_dataBytes;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
