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

public:
    SHAMapAddNode();

    /**
     * Record one node that was rejected.
     */
    void
    incInvalid();

    /**
     * Record one node that was hooked into the map.
     */
    void
    incUseful();

    /**
     * Record one node the map already held.
     *
     * Counted separately from a useful node: it is not new data, but it is
     * also not a rejection - see isGood().
     */
    void
    incDuplicate();

    /**
     * @return How many nodes were hooked into the map, which isUseful() only
     *         reports the presence of.
     */
    [[nodiscard]] int
    getGood() const;
    /**
     * @return How many nodes were rejected, which isInvalid() only reports the
     *         presence of. A batch that stops on its first bad node counts one;
     *         one that carries on counts each.
     */
    [[nodiscard]] int
    getBad() const;

    /**
     * @return How many nodes the batch already held, counting as neither good
     *         nor bad.
     */
    [[nodiscard]] int
    getDuplicate() const;

    /**
     * Whether the batch overall was worth the exchange: nodes accepted or
     * already held outnumber the ones rejected.
     *
     * A duplicate counts on the accepted side, since the peer answered a
     * request rather than sent something unasked for; see incDuplicate().
     *
     * @return Whether the batch was good.
     */
    [[nodiscard]] bool
    isGood() const;

    /**
     * Whether any node in the batch was rejected.
     *
     * @return Whether at least one node was bad.
     */
    [[nodiscard]] bool
    isInvalid() const;

    /**
     * Whether any node in the batch was hooked into the map.
     *
     * @return Whether at least one node was useful.
     */
    [[nodiscard]] bool
    isUseful() const;

    /**
     * @return A verdict recording one duplicate node.
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
     * Clear every count back to zero.
     */
    void
    reset();

    /**
     * Render the tally as a log line.
     *
     * A format rather than an API: a caller that needs the counts themselves
     * should read them through getGood(), getBad() and getDuplicate() instead
     * of parsing this.
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

inline void
SHAMapAddNode::reset()
{
    good_ = bad_ = duplicate_ = 0;
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

    return *this;
}

}  // namespace xrpl
