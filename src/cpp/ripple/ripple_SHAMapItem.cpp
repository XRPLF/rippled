
class SHAMap;

DECLARE_INSTANCE(SHAMapItem);

SHAMapItem::SHAMapItem (uint256 const& tag, Blob const& data)
	: mTag (tag)
    , mData (data)
{
}

SHAMapItem::SHAMapItem(uint256 const& tag, const Serializer& data)
	: mTag (tag)
    , mData (data.peekData())
{
}
