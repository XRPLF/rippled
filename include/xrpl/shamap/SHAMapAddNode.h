#pragma once

#include <string>

namespace xrpl {

// results of adding nodes
class SHAMapAddNode
{
private:
    int mGood_;
    int mBad_;
    int mDuplicate_;

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

inline SHAMapAddNode::SHAMapAddNode() : mGood_(0), mBad_(0), mDuplicate_(0)
{
}

inline SHAMapAddNode::SHAMapAddNode(int good, int bad, int duplicate)
    : mGood_(good), mBad_(bad), mDuplicate_(duplicate)
{
}

inline void
SHAMapAddNode::incInvalid()
{
    ++mBad_;
}

inline void
SHAMapAddNode::incUseful()
{
    ++mGood_;
}

inline void
SHAMapAddNode::incDuplicate()
{
    ++mDuplicate_;
}

inline void
SHAMapAddNode::reset()
{
    mGood_ = mBad_ = mDuplicate_ = 0;
}

inline int
SHAMapAddNode::getGood() const
{
    return mGood_;
}

inline bool
SHAMapAddNode::isInvalid() const
{
    return mBad_ > 0;
}

inline bool
SHAMapAddNode::isUseful() const
{
    return mGood_ > 0;
}

inline SHAMapAddNode&
SHAMapAddNode::operator+=(SHAMapAddNode const& n)
{
    mGood_ += n.mGood_;
    mBad_ += n.mBad_;
    mDuplicate_ += n.mDuplicate_;

    return *this;
}

inline bool
SHAMapAddNode::isGood() const
{
    return (mGood_ + mDuplicate_) > mBad_;
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
    if (mGood_ > 0)
    {
        ret.append("good:");
        ret.append(std::to_string(mGood_));
    }
    if (mBad_ > 0)
    {
        if (!ret.empty())
            ret.append(" ");
        ret.append("bad:");
        ret.append(std::to_string(mBad_));
    }
    if (mDuplicate_ > 0)
    {
        if (!ret.empty())
            ret.append(" ");
        ret.append("dupe:");
        ret.append(std::to_string(mDuplicate_));
    }
    if (ret.empty())
        ret = "no nodes processed";
    return ret;
}

}  // namespace xrpl
