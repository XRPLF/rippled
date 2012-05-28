#include "PackedMessage.h"


void PackedMessage::encodeHeader(unsigned size, int type) 
{
	assert(mBuffer.size() >= HEADER_SIZE);
	mBuffer[0] = static_cast<boost::uint8_t>((size >> 24) & 0xFF);
	mBuffer[1] = static_cast<boost::uint8_t>((size >> 16) & 0xFF);
	mBuffer[2] = static_cast<boost::uint8_t>((size >> 8) & 0xFF);
	mBuffer[3] = static_cast<boost::uint8_t>(size & 0xFF);
	mBuffer[4] = static_cast<boost::uint8_t>((type >> 8) & 0xFF);
	mBuffer[5] = static_cast<boost::uint8_t>(type & 0xFF);
}

PackedMessage::PackedMessage(const ::google::protobuf::Message &message, int type)
{
	unsigned msg_size = message.ByteSize();
	assert(msg_size);
	mBuffer.resize(HEADER_SIZE + msg_size);
	encodeHeader(msg_size, type);
	if (msg_size)
	{
		message.SerializeToArray(&mBuffer[HEADER_SIZE], msg_size);
#ifdef DEBUG
		std::cerr << "PackedMessage: type=" << type << ", datalen=" << msg_size << std::endl;
#endif
	}
}

bool PackedMessage::operator == (const PackedMessage& other)
{
	return (mBuffer == other.mBuffer);
}

unsigned PackedMessage::getLength(std::vector<uint8_t>& buf)
{
	if(buf.size() < HEADER_SIZE)
		return 0;

	int ret = buf[0];
	ret <<= 8; ret |= buf[1]; ret <<= 8; ret |= buf[2];	ret <<= 8; ret |= buf[3];
	return ret;
}

int PackedMessage::getType(std::vector<uint8_t>& buf)
{
	if(buf.size() < HEADER_SIZE)
		return 0;

	int ret = buf[4];
	ret <<= 8; ret |= buf[5];
	return ret;
}
