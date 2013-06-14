#ifndef RIPPLE_SHAMAPITEM_H
#define RIPPLE_SHAMAPITEM_H

DEFINE_INSTANCE (SHAMapItem);

// an item stored in a SHAMap
class SHAMapItem : public IS_INSTANCE (SHAMapItem)
{
public:
    typedef boost::shared_ptr<SHAMapItem>           pointer;
    typedef const boost::shared_ptr<SHAMapItem>&    ref;

public:
    explicit SHAMapItem (uint256 const & tag) : mTag (tag)
    {
        ;
    }
    explicit SHAMapItem (Blob const & data); // tag by hash
    SHAMapItem (uint256 const & tag, Blob const & data);
    SHAMapItem (uint256 const & tag, const Serializer & s);

    uint256 const& getTag () const
    {
        return mTag;
    }
    Blob getData () const
    {
        return mData.getData ();
    }
    Blob const& peekData () const
    {
        return mData.peekData ();
    }
    Serializer& peekSerializer ()
    {
        return mData;
    }
    void addRaw (Blob & s) const
    {
        s.insert (s.end (), mData.begin (), mData.end ());
    }

    void updateData (Blob const & data)
    {
        mData = data;
    }

    bool operator== (const SHAMapItem & i) const
    {
        return mTag == i.mTag;
    }
    bool operator!= (const SHAMapItem & i) const
    {
        return mTag != i.mTag;
    }
    bool operator== (uint256 const & i) const
    {
        return mTag == i;
    }
    bool operator!= (uint256 const & i) const
    {
        return mTag != i;
    }

#if 0
    // This code is comment out because it is unused.  It could work.
    bool operator< (const SHAMapItem & i) const
    {
        return mTag < i.mTag;
    }
    bool operator> (const SHAMapItem & i) const
    {
        return mTag > i.mTag;
    }
    bool operator<= (const SHAMapItem & i) const
    {
        return mTag <= i.mTag;
    }
    bool operator>= (const SHAMapItem & i) const
    {
        return mTag >= i.mTag;
    }

    bool operator< (uint256 const & i) const
    {
        return mTag < i;
    }
    bool operator> (uint256 const & i) const
    {
        return mTag > i;
    }
    bool operator<= (uint256 const & i) const
    {
        return mTag <= i;
    }
    bool operator>= (uint256 const & i) const
    {
        return mTag >= i;
    }
#endif

    virtual void dump ();

private:
    uint256 mTag;
    Serializer mData;
};

#endif
