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
    void
    incInvalid();
    void
    incUseful();
    void
    incDuplicate();
    void
    reset();
    [[nodiscard]] int
    getGood() const;
    [[nodiscard]] bool
    isGood() const;
    [[nodiscard]] bool
    isInvalid() const;
    [[nodiscard]] bool
    isUseful() const;
    [[nodiscard]] std::string
    get() const;

    SHAMapAddNode&
    operator+=(SHAMapAddNode const& n);

    static SHAMapAddNode
    duplicate();
    static SHAMapAddNode
    useful();
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
