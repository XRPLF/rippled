
// Unity build file for websocket
//

// Must come first to prevent compile errors
#include "websocketpp/src/uri.cpp"

#include "websocketpp/src/base64/base64.cpp"
#include "websocketpp/src/rng/boost_rng.cpp"
#include "websocketpp/src/messages/data.cpp"
#include "websocketpp/src/processors/hybi_header.cpp"
#include "websocketpp/src/processors/hybi_util.cpp"
#include "websocketpp/src/md5/md5.c"
#include "websocketpp/src/network_utilities.cpp"
#include "websocketpp/src/sha1/sha1.cpp"

