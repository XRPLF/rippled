#pragma once

#include <string>

namespace xrpl {

/** Accumulates the outcome of adding one or more nodes to a SHAMap during sync.
 *
 *  During ledger synchronization, raw trie nodes received from peers are
 *  classified as useful (new and hash-verified), duplicate (already present),
 *  or invalid (corrupt, hash-mismatched, or structurally wrong). This class
 *  collects those three counts so callers can assess whether a peer's
 *  contribution was helpful, harmless, or harmful.
 *
 *  Instances are typically constructed via the static factory methods
 *  (`useful()`, `duplicate()`, `invalid()`) as single-count return values
 *  from `SHAMap::addRootNode()` and `SHAMap::addKnownNode()`. They are then
 *  aggregated with `operator+=` across a batch of nodes in higher-level
 *  acquisition code such as `InboundLedger::receiveNode()`.
 *
 *  @note Duplicates count on the positive side of `isGood()` because
 *      receiving a node you already have is benign. Only `mBad` accumulates
 *      evidence of peer misbehavior.
 *  @see SHAMap::addRootNode, SHAMap::addKnownNode
 */
class SHAMapAddNode
{
private:
    int good_;
    int bad_;
    int duplicate_;

public:
    /** Construct a zero-count accumulator. */
    SHAMapAddNode();

    /** Record one invalid node (corrupt, hash-mismatched, or structurally wrong). */
    void
    incInvalid();

    /** Record one useful node (new, hash-verified, and written to the map). */
    void
    incUseful();

    /** Record one duplicate node (valid but already present in the map). */
    void
    incDuplicate();

    /** Reset all counters to zero. */
    void
    reset();

    /** Return the count of useful (good) nodes recorded.
     *
     *  @return The number of new, hash-verified nodes added.
     */
    [[nodiscard]] int
    getGood() const;

    /** Return whether the exchange was net non-harmful.
     *
     *  Returns `true` when `(good + duplicate) > bad`. Duplicates count
     *  positively because they are benign; only bad nodes are evidence of
     *  misbehavior. Used by `TransactionAcquire::takeNodes()` and
     *  `InboundLedger` to decide whether to continue processing a peer's
     *  contribution.
     *
     *  @return `true` if the peer's contribution was not net-harmful.
     */
    [[nodiscard]] bool
    isGood() const;

    /** Return whether any invalid nodes were recorded.
     *
     *  @return `true` if at least one node was classified as invalid.
     */
    [[nodiscard]] bool
    isInvalid() const;

    /** Return whether actual forward progress was made.
     *
     *  Answers "did we receive at least one new node we didn't already have?"
     *  Strictly stronger than `isGood()`: duplicates do not satisfy this
     *  predicate. Used by `InboundLedger` to advance the `progress_` flag
     *  and decide whether sync moved forward.
     *
     *  @return `true` if at least one useful (new) node was added.
     */
    [[nodiscard]] bool
    isUseful() const;

    /** Format the counters as a human-readable string for journal output.
     *
     *  Emits only the non-zero counters, e.g., `"good:3 dupe:1"` or
     *  `"bad:2"`. Returns `"no nodes processed"` when all three counts are
     *  zero.
     *
     *  @return A compact diagnostic string suitable for debug-level logging.
     */
    [[nodiscard]] std::string
    get() const;

    /** Combine another accumulator into this one by summing all three counters.
     *
     *  Used to aggregate results across a batch of nodes, e.g., in
     *  `InboundLedger::receiveNode()` where `san += map.addKnownNode(...)` is
     *  called in a loop.
     *
     *  @param n The accumulator to add.
     *  @return A reference to this accumulator.
     */
    SHAMapAddNode&
    operator+=(SHAMapAddNode const& n);

    /** Construct a single-duplicate-count instance.
     *
     *  Returned by `addRootNode` / `addKnownNode` when the node was valid
     *  but already present in the map.
     *
     *  @return An instance with `duplicate = 1`, `good = 0`, `bad = 0`.
     */
    static SHAMapAddNode
    duplicate();

    /** Construct a single-useful-count instance.
     *
     *  Returned by `addRootNode` / `addKnownNode` when the node was new
     *  and successfully verified and inserted.
     *
     *  @return An instance with `good = 1`, `bad = 0`, `duplicate = 0`.
     */
    static SHAMapAddNode
    useful();

    /** Construct a single-invalid-count instance.
     *
     *  Returned by `addRootNode` / `addKnownNode` when the node failed hash
     *  verification, had a structural mismatch, or arrived on an empty branch.
     *
     *  @return An instance with `bad = 1`, `good = 0`, `duplicate = 0`.
     */
    static SHAMapAddNode
    invalid();

private:
    SHAMapAddNode(int good, int bad, int duplicate);
};

inline SHAMapAddNode::SHAMapAddNode() : good_(0), bad_(0), duplicate_(0)
{
}

inline SHAMapAddNode::SHAMapAddNode(int good, int bad, int duplicate)
    : good_(good), bad_(bad), duplicate_(duplicate)
{
}

inline void
SHAMapAddNode::incInvalid()
{
    ++bad_;
}

inline void
SHAMapAddNode::incUseful()
{
    ++good_;
}

inline void
SHAMapAddNode::incDuplicate()
{
    ++duplicate_;
}

inline void
SHAMapAddNode::reset()
{
    good_ = bad_ = duplicate_ = 0;
}

inline int
SHAMapAddNode::getGood() const
{
    return good_;
}

inline bool
SHAMapAddNode::isInvalid() const
{
    return bad_ > 0;
}

inline bool
SHAMapAddNode::isUseful() const
{
    return good_ > 0;
}

inline SHAMapAddNode&
SHAMapAddNode::operator+=(SHAMapAddNode const& n)
{
    good_ += n.good_;
    bad_ += n.bad_;
    duplicate_ += n.duplicate_;

    return *this;
}

inline bool
SHAMapAddNode::isGood() const
{
    return (good_ + duplicate_) > bad_;
}

inline SHAMapAddNode
SHAMapAddNode::duplicate()
{
    return SHAMapAddNode(0, 0, 1);
}

inline SHAMapAddNode
SHAMapAddNode::useful()
{
    return SHAMapAddNode(1, 0, 0);
}

inline SHAMapAddNode
SHAMapAddNode::invalid()
{
    return SHAMapAddNode(0, 1, 0);
}

inline std::string
SHAMapAddNode::get() const
{
    std::string ret;
    if (good_ > 0)
    {
        ret.append("good:");
        ret.append(std::to_string(good_));
    }
    if (bad_ > 0)
    {
        if (!ret.empty())
            ret.append(" ");
        ret.append("bad:");
        ret.append(std::to_string(bad_));
    }
    if (duplicate_ > 0)
    {
        if (!ret.empty())
            ret.append(" ");
        ret.append("dupe:");
        ret.append(std::to_string(duplicate_));
    }
    if (ret.empty())
        ret = "no nodes processed";
    return ret;
}

}  // namespace xrpl
