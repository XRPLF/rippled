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

#ifndef WEBSOCKETPP_SOCKET_AUTOTLS_HPP
#define WEBSOCKETPP_SOCKET_AUTOTLS_HPP

// Note that AutoSocket.h must be included before this file

namespace websocketpp {
namespace socket {

template <typename endpoint_type>
class autotls {
public:
    typedef autotls<endpoint_type> type;
    typedef AutoSocket autotls_socket;
    typedef boost::shared_ptr<autotls_socket> autotls_socket_ptr;
    
    // should be private friended
    boost::asio::io_service& get_io_service() {
        return m_io_service;
    }

    static void handle_shutdown(autotls_socket_ptr, const boost::system::error_code&) {
    }

    void set_secure_only() {
        m_secure_only = true;
	}

	void set_plain_only() {
		m_plain_only = true;
	}

    // should be private friended?
    autotls_socket::handshake_type get_handshake_type() {
        if (static_cast< endpoint_type* >(this)->is_server()) {
            return boost::asio::ssl::stream_base::server;
        } else {
            return boost::asio::ssl::stream_base::client;
        }
    }

    class handler_interface {
    public:
    	virtual ~handler_interface() {}

        virtual void on_tcp_init() {};
        virtual boost::asio::ssl::context& get_ssl_context () = 0;
    };
    
    // Connection specific details
    template <typename connection_type>
    class connection {
    public:
        // should these two be public or protected. If protected, how?
        autotls_socket::lowest_layer_type& get_raw_socket() {
            return m_socket_ptr->lowest_layer();
        }
        
        autotls_socket& get_socket() {
            return *m_socket_ptr;
        }
        
        bool is_secure() {
            return m_socket_ptr->isSecure();
        }
    protected:
        connection(autotls<endpoint_type>& e)
         : m_endpoint(e)
         , m_connection(static_cast< connection_type& >(*this)) {}
        
        void init() {
            boost::asio::ssl::context& ssl_context (
                m_connection.get_handler()->get_ssl_context());
            
            m_socket_ptr = autotls_socket_ptr (new autotls_socket (
                m_endpoint.get_io_service(), ssl_context, m_endpoint.m_secure_only,
                    m_endpoint.m_plain_only));
        }
        
        void async_init(boost::function<void(const boost::system::error_code&)> callback)
        {
            m_connection.get_handler()->on_tcp_init();
            
            // wait for TLS handshake
            // TODO: configurable value
            m_connection.register_timeout(5000,
                                          fail::status::TIMEOUT_TLS,
                                          "Timeout on TLS handshake");
            
            m_socket_ptr->async_handshake(
                m_endpoint.get_handshake_type(),
                boost::bind(
                    &connection<connection_type>::handle_init,
                    this,
                    callback,
                    boost::asio::placeholders::error
                )
            );
        }
        
        void handle_init(socket_init_callback callback,const boost::system::error_code& error) {
            m_connection.cancel_timeout();
            callback(error);
        }
        
        // note, this function for some reason shouldn't/doesn't need to be 
        // called for plain HTTP connections. not sure why.
        bool shutdown() {
            boost::system::error_code ignored_ec;
            
            m_socket_ptr->async_shutdown( // Don't block on connection shutdown DJS
                boost::bind(
		            &autotls<endpoint_type>::handle_shutdown,
                    m_socket_ptr,
                    boost::asio::placeholders::error
				)
			);
            
            if (ignored_ec) {
                return false;
            } else {
                return true;
            }
        }
    private:
        boost::shared_ptr<boost::asio::ssl::context>    m_context_ptr;
        autotls_socket_ptr                              m_socket_ptr;
        autotls<endpoint_type>&                         m_endpoint;
        connection_type&                                m_connection;
    };
protected:
    autotls (boost::asio::io_service& m) : m_io_service(m), m_secure_only(false), m_plain_only(false) {}
private:
    boost::asio::io_service&    m_io_service;
    bool						m_secure_only;
    bool						m_plain_only;
};
    
} // namespace socket
} // namespace websocketpp

#endif // WEBSOCKETPP_SOCKET_AUTOTLS_HPP
