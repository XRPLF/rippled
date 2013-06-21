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

#ifndef CHAT_CLIENT_HANDLER_HPP
#define CHAT_CLIENT_HANDLER_HPP

// com.zaphoyd.websocketpp.chat protocol
// 
// client messages:
// alias [UTF8 text, 16 characters max]
// msg [UTF8 text]
// 
// server messages:
// {"type":"msg","sender":"<sender>","value":"<msg>" }
// {"type":"participants","value":[<participant>,...]}

#include <boost/shared_ptr.hpp>

#include "../../src/roles/client.hpp"
#include "../../src/websocketpp.hpp"

#include <map>
#include <string>
#include <queue>

using websocketpp::client;

namespace websocketchat {

class chat_client_handler : public client::handler {
public:
    chat_client_handler() {}
    virtual ~chat_client_handler() {}
    
    void on_fail(connection_ptr con);
    
    // connection to chat room complete
    void on_open(connection_ptr con);

    // connection to chat room closed
    void on_close(connection_ptr con);
    
    // got a new message from server
    void on_message(connection_ptr con, message_ptr msg);
    
    // CLIENT API
    void send(const std::string &msg);
    void close();

private:
    void decode_server_msg(const std::string &msg);
    
    // list of other chat participants
    std::set<std::string> m_participants;
    std::queue<std::string> m_msg_queue;
    connection_ptr m_con;
};

typedef boost::shared_ptr<chat_client_handler> chat_client_handler_ptr;

}
#endif // CHAT_CLIENT_HANDLER_HPP
