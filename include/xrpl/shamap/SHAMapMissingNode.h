/** @file
 *  Defines the `SHAMapType` enum and the `SHAMapMissingNode` exception,
 *  forming the error-signalling contract for the SHAMap subsystem.
 */

#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <iosfwd>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace xrpl {

/** Classifies the role of a SHAMap within the ledger.
 *
 *  Every closed ledger contains two SHAMaps: a `TRANSACTION` tree holding the
 *  set of transactions included in that ledger and a `STATE` tree holding the
 *  full account-state database. `FREE` trees are ephemeral and not tied to a
 *  finalized ledger (e.g., consensus transaction sets).
 *
 *  @note The underlying integer values appear in wire-protocol serialization
 *      for sync packets and must not be changed.
 */
enum class SHAMapType {
    TRANSACTION = 1,  /**< A tree of transactions included in a ledger. */
    STATE = 2,        /**< A tree of account-state objects at a ledger's close. */
    FREE = 3,         /**< An ephemeral tree not associated with a finalized ledger. */
};

/** Convert a `SHAMapType` to a human-readable label for logging.
 *
 *  Returns `"Transaction Tree"`, `"State Tree"`, or `"Free Tree"` for the
 *  three defined enumerators. For any out-of-range value the numeric
 *  underlying integer is returned as a string rather than producing
 *  undefined behavior.
 *
 *  @param t The tree type to convert.
 *  @return A string label suitable for log messages.
 */
inline std::string
to_string(SHAMapType t)
{
    switch (t)
    {
        case SHAMapType::TRANSACTION:
            return "Transaction Tree";
        case SHAMapType::STATE:
            return "State Tree";
        case SHAMapType::FREE:
            return "Free Tree";
        default:
            return std::to_string(safeCast<std::underlying_type_t<SHAMapType>>(t));
    }
}

/** Exception thrown when SHAMap traversal encounters a locally absent node.
 *
 *  This is a routine operational condition in a distributed ledger node:
 *  partially-synced or historical ledgers may have gaps in local storage.
 *  The exception propagates upward through the traversal call stack so that
 *  each subsystem can apply its own recovery policy:
 *
 *  - `LedgerCleaner` and `LedgerMaster` catch it at `warn` level and schedule
 *    a peer fetch via `getInboundLedgers().acquire()`.
 *  - `RCLConsensus` treats it as fatal during consensus processing, logging at
 *    `error` level and re-throwing.
 *  - `Ledger.cpp` catches it silently and treats the incomplete tree as an
 *    invalid ledger.
 *
 *  The `what()` message is built eagerly at construction time since it is the
 *  primary data surface available to catch sites, all of which log it directly.
 *
 *  @see SHAMapType
 */
class SHAMapMissingNode : public std::runtime_error
{
public:
    /** Construct with the hash of the absent node.
     *
     *  Used when the tree descends toward a child whose hash is known (stored
     *  in the parent inner node) but whose backing data is not in local storage.
     *  Produces the message `"Missing Node: <tree type>: hash <hex>"`.
     *
     *  @param t    The type of tree in which the node was not found.
     *  @param hash The SHA-512Half digest identifying the absent node.
     */
    SHAMapMissingNode(SHAMapType t, SHAMapHash const& hash)
        : std::runtime_error("Missing Node: " + to_string(t) + ": hash " + to_string(hash))
    {
    }

    /** Construct with the item key that could not be located.
     *
     *  Used when the failure is expressed at the level of a leaf identifier
     *  (e.g., an account ID or transaction ID) rather than a structural node
     *  hash — typically when a key-based lookup descends far enough to
     *  determine the leaf should exist but cannot find it.
     *  Produces the message `"Missing Node: <tree type>: id <hex>"`.
     *
     *  @param t  The type of tree in which the item was not found.
     *  @param id The 256-bit key of the item that could not be located.
     */
    SHAMapMissingNode(SHAMapType t, uint256 const& id)
        : std::runtime_error("Missing Node: " + to_string(t) + ": id " + to_string(id))
    {
    }
};

}  // namespace xrpl
