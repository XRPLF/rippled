#include "PackedMessage.h"


void PackedMessage::encodeHeader(unsigned size,int type) 
{
	assert(mBuffer.size() >= HEADER_SIZE);
	mBuffer[0] = static_cast<boost::uint8_t>((size >> 24) & 0xFF);
	mBuffer[1] = static_cast<boost::uint8_t>((size >> 16) & 0xFF);
	mBuffer[2] = static_cast<boost::uint8_t>((size >> 8) & 0xFF);
	mBuffer[3] = static_cast<boost::uint8_t>(size & 0xFF);
	mBuffer[4] = static_cast<boost::uint8_t>((type >> 8) & 0xFF);
	mBuffer[5] = static_cast<boost::uint8_t>(type & 0xFF);
}


PackedMessage::PackedMessage(MessagePointer msg,int type)
	: mMsg(msg)
{
	unsigned msg_size = mMsg->ByteSize();
	mBuffer.resize(HEADER_SIZE + msg_size);
	encodeHeader(msg_size,type);
	mMsg->SerializeToArray(&mBuffer[HEADER_SIZE], msg_size);
}

bool PackedMessage::operator == (const PackedMessage& other)
{
	return(mBuffer==other.mBuffer);
}

// TODO: this is nonsense
unsigned PackedMessage::getLength(std::vector<uint8_t>& buf)
{
	if(buf.size() < HEADER_SIZE) return 0;

	int ret=buf[0];
	ret= ret << 8;
	ret= ret | buf[1];
	ret= ret << 8;
	ret= ret | buf[2];
	ret= ret << 8;
	ret= ret | buf[3];

	return(ret);
}

int PackedMessage::getType(std::vector<uint8_t>& buf)
{
	if(buf.size() < HEADER_SIZE) return 0;

	int ret=buf[4];
	ret= ret << 8;
	ret= ret | buf[5];
	return(ret);
}


