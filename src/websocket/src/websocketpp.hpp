/*
 * Copyright (c) 2011, Peter Thorson. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the WebSocket++ Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL PETER THORSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#ifndef WEBSOCKETPP_HPP
#define WEBSOCKETPP_HPP

#include "common.hpp"
#include "endpoint.hpp"

namespace websocketpp {
#ifdef WEBSOCKETPP_ROLE_SERVER_HPP
    #ifdef WEBSOCKETPP_SOCKET_PLAIN_HPP
        typedef websocketpp::endpoint<websocketpp::role::server,
                                      websocketpp::socket::plain> server;
    #endif
    #ifdef WEBSOCKETPP_SOCKET_TLS_HPP
        typedef websocketpp::endpoint<websocketpp::role::server,
                                      websocketpp::socket::tls> server_tls;
    #endif
    #ifdef WEBSOCKETPP_SOCKET_AUTOTLS_HPP
        typedef websocketpp::endpoint<websocketpp::role::server,
                                      websocketpp::socket::autotls> server_autotls;
    #endif
#endif


#ifdef WEBSOCKETPP_ROLE_CLIENT_HPP
    #ifdef WEBSOCKETPP_SOCKET_PLAIN_HPP
        typedef websocketpp::endpoint<websocketpp::role::client,
                                      websocketpp::socket::plain> client;
    #endif
    #ifdef WEBSOCKETPP_SOCKET_TLS_HPP
        typedef websocketpp::endpoint<websocketpp::role::client,
                                      websocketpp::socket::tls> client_tls;
    #endif
#endif
} // namespace websocketpp

#endif // WEBSOCKETPP_HPP
