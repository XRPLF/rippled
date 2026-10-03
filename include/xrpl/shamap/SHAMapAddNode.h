#pragma once

#include <string>

namespace xrpl {

// results of adding nodes
class SHAMapAddNode
{
private:
    int good_;
    int bad_;
    int duplicate_;

    // Whether a node in the batch proved the map impossible. A fact about the batch rather than
    // about the map, so a caller can tell this batch's doing from a concurrent walk's verdict.
    bool invalidatedMap_;

public:
    SHAMapAddNode();

    /**
     * Record one node that was rejected.
     */
    void
    incInvalid();

    /**
     * Record one node that produced a good result.
     */
    void
    incUseful();

    /**
     * Record one node that was not needed: the map already held it, or the map
     * was no longer taking nodes. isGood() counts it on the accepted side.
     */
    void
    incDuplicate();

    /**
     * @return How many nodes produced a good result.
     */
    [[nodiscard]] int
    getGood() const;

    /**
     * @return How many nodes this code rejected, which is not how many the
     *         batch was given: a batch that stops at its first bad node counts
     *         one, and a batch that carries on counts each.
     */
    [[nodiscard]] int
    getBad() const;

    /**
     * @return How many nodes were not needed, whether already held or offered
     *         to a map no longer taking nodes, tallied apart from the good and
     *         bad counts.
     */
    [[nodiscard]] int
    getDuplicate() const;

    /**
     * Whether nodes accepted or not needed outnumber the ones rejected.
     *
     * @return Whether the batch was good.
     */
    [[nodiscard]] bool
    isGood() const;

    /**
     * @return Whether at least one node in the batch was rejected.
     */
    [[nodiscard]] bool
    isInvalid() const;

    /**
     * @return Whether at least one node in the batch produced a good result.
     */
    [[nodiscard]] bool
    isUseful() const;

    /**
     * @return A verdict recording one node that was not needed.
     */
    static SHAMapAddNode
    duplicate();

    /**
     * @return A verdict recording one useful node.
     */
    static SHAMapAddNode
    useful();

    /**
     * @return A verdict recording one invalid node.
     */
    static SHAMapAddNode
    invalid();

    /**
     * @return A verdict recording one invalid node that proved the map
     *         impossible.
     */
    static SHAMapAddNode
    mapInvalidated();

    /**
     * Whether a node in the batch proved the map impossible.
     *
     * Reports what the batch did rather than what the map now holds, so a
     * verdict another thread's walk reached is not read as this batch's. The
     * two differ: SHAMap::setInvalid() is written with no lock held, by a
     * getMissingNodes() walk its caller runs lock-free.
     *
     * @return Whether a node in the batch invalidated the map.
     */
    [[nodiscard]] bool
    invalidatedMap() const;

    /**
     * Clear every count back to zero.
     */
    void
    reset();

    /**
     * Render the tally as a log line.
     *
     * @return The tally, e.g. "good:2 bad:1 dupe:1", or "no nodes processed" if
     *         every count is zero.
     */
    [[nodiscard]] std::string
    get() const;

    /**
     * Add another verdict's counts into this one.
     *
     * @param n The verdict to add.
     * @return This verdict, updated.
     */
    SHAMapAddNode&
    operator+=(SHAMapAddNode const& n);

private:
    SHAMapAddNode(int good, int bad, int duplicate, bool invalidatedMap = false);
};

inline SHAMapAddNode::SHAMapAddNode() : good_(0), bad_(0), duplicate_(0), invalidatedMap_(false)
{
}

inline SHAMapAddNode::SHAMapAddNode(int good, int bad, int duplicate, bool invalidatedMap)
    : good_(good), bad_(bad), duplicate_(duplicate), invalidatedMap_(invalidatedMap)
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

inline int
SHAMapAddNode::getGood() const
{
    return good_;
}

inline int
SHAMapAddNode::getBad() const
{
    return bad_;
}

inline int
SHAMapAddNode::getDuplicate() const
{
    return duplicate_;
}

inline bool
SHAMapAddNode::isGood() const
{
    return (good_ + duplicate_) > bad_;
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

inline bool
SHAMapAddNode::invalidatedMap() const
{
    return invalidatedMap_;
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

inline SHAMapAddNode
SHAMapAddNode::mapInvalidated()
{
    return SHAMapAddNode(0, 1, 0, true);
}

inline void
SHAMapAddNode::reset()
{
    good_ = bad_ = duplicate_ = 0;
    invalidatedMap_ = false;
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

inline SHAMapAddNode&
SHAMapAddNode::operator+=(SHAMapAddNode const& n)
{
    good_ += n.good_;
    bad_ += n.bad_;
    duplicate_ += n.duplicate_;

    // Or-ed, not summed: one node in the batch having proved the map impossible is the whole fact.
    invalidatedMap_ = invalidatedMap_ || n.invalidatedMap_;

    return *this;
}

}  // namespace xrpl
