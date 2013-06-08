
// Unity build file for websocket
//

// VFALCO TODO Fix this right. It's not really needed
//				 to include this file, its a hack to keep
//				 __STDC_LIMIT_MACROS from generating redefinition warnings
#include "websocketpp/src/rng/boost_rng.hpp"

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

