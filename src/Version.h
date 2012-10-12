#ifndef __VERSIONS__
#define __VERSIONS__
//
// Versions
//

#define SERVER_VERSION_MAJOR		0
#define SERVER_VERSION_MINOR		7
#define SERVER_VERSION_SUB			"-a"
#define SERVER_NAME					"Ripple"

#define SV_STRINGIZE(x)				SV_STRINGIZE2(x)
#define SV_STRINGIZE2(x)			#x
#define SERVER_VERSION				\
	(SERVER_NAME "-" SV_STRINGIZE(SERVER_VERSION_MAJOR) "." SV_STRINGIZE(SERVER_VERSION_MINOR) SERVER_VERSION_SUB)

// Version we prefer to speak:
#define PROTO_VERSION_MAJOR			1
#define PROTO_VERSION_MINOR			2

// Version we will speak to:
#define MIN_PROTO_MAJOR				1
#define MIN_PROTO_MINOR				2

#define MAKE_VERSION_INT(maj,min)	((maj << 16) | min)
#define GET_VERSION_MAJOR(ver)		(ver >> 16)
#define GET_VERSION_MINOR(ver)		(ver & 0xff)

#endif
// vim:ts=4
