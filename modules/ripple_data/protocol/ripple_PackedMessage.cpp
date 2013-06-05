

PackedMessage::PackedMessage (::google::protobuf::Message const& message, int type)
{
	unsigned const messageBytes = message.ByteSize ();

    assert (messageBytes != 0);
	
    mBuffer.resize (kHeaderBytes + messageBytes);

    encodeHeader (messageBytes, type);
	
    if (messageBytes != 0)
	{
		message.SerializeToArray (&mBuffer [PackedMessage::kHeaderBytes], messageBytes);

#ifdef DEBUG
//		std::cerr << "PackedMessage: type=" << type << ", datalen=" << msg_size << std::endl;
#endif
	}
}

bool PackedMessage::operator== (PackedMessage const& other) const
{
	return mBuffer == other.mBuffer;
}

unsigned PackedMessage::getLength (std::vector <uint8_t> const& buf)
{
    unsigned result;

	if (buf.size() >= PackedMessage::kHeaderBytes)
    {
	    result = buf [0];
	    result <<= 8; result |= buf [1];
        result <<= 8; result |= buf [2];
        result <<= 8; result |= buf [3];
    }
    else
    {
        result = 0;
    }

    return result;
}

int PackedMessage::getType (std::vector<uint8_t> const& buf)
{
	if(buf.size() < PackedMessage::kHeaderBytes)
		return 0;

	int ret = buf[4];
	ret <<= 8; ret |= buf[5];
	return ret;
}

void PackedMessage::encodeHeader (unsigned size, int type) 
{
	assert(mBuffer.size() >= PackedMessage::kHeaderBytes);
	mBuffer[0] = static_cast<boost::uint8_t>((size >> 24) & 0xFF);
	mBuffer[1] = static_cast<boost::uint8_t>((size >> 16) & 0xFF);
	mBuffer[2] = static_cast<boost::uint8_t>((size >> 8) & 0xFF);
	mBuffer[3] = static_cast<boost::uint8_t>(size & 0xFF);
	mBuffer[4] = static_cast<boost::uint8_t>((type >> 8) & 0xFF);
	mBuffer[5] = static_cast<boost::uint8_t>(type & 0xFF);
}
