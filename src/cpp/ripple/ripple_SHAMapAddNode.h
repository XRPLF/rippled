#ifndef RIPPLE_SHAMAPADDNODE_H
#define RIPPLE_SHAMAPADDNODE_H

// results of adding nodes
class SHAMapAddNode
{
public:
    SHAMapAddNode ()
        : mInvalid (false)
        , mUseful (false)
    {
    }

    void setInvalid ()
    {
        mInvalid = true;
    }
    void setUseful ()
    {
        mUseful = true;
    }
    void reset ()
    {
        mInvalid = false;
        mUseful = false;
    }

    bool isInvalid () const
    {
        return mInvalid;
    }
    bool isUseful () const
    {
        return mUseful;
    }

    bool combine (SHAMapAddNode const& n)
    {
        // VFALCO NOTE What is the meaning of these lines?

        if (n.mInvalid)
        {
            mInvalid = true;
            return false;
        }

        if (n.mUseful)
            mUseful = true;

        return true;
    }

    operator bool () const
    {
        return !mInvalid;
    }

    static SHAMapAddNode okay ()
    {
        return SHAMapAddNode (false, false);
    }
    static SHAMapAddNode useful ()
    {
        return SHAMapAddNode (false, true);
    }
    static SHAMapAddNode invalid ()
    {
        return SHAMapAddNode (true, false);
    }

private:
    SHAMapAddNode (bool i, bool u)
        : mInvalid (i)
        , mUseful (u)
    {
    }

    bool mInvalid;
    bool mUseful;
};

#endif
