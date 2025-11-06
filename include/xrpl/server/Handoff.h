#ifndef XRPL_SERVER_HANDOFF_H_INCLUDED
#define XRPL_SERVER_HANDOFF_H_INCLUDED

#include <xrpl/server/Writer.h>

#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/message.hpp>

#include <memory>

namespace ripple {

using http_request_type =
    boost::beast::http::request<boost::beast::http::dynamic_body>;

using http_response_type =
    boost::beast::http::response<boost::beast::http::dynamic_body>;

/** Used to indicate the result of a server connection handoff. */
struct Handoff
{
    // When `true`, the Session will close the socket. The
    // Handler may optionally take socket ownership using std::move
    bool moved = false;

    // If response is set, this determines the keep alive
    bool keep_alive = false;

    // When set, this will be sent back
    std::shared_ptr<Writer> response;

    bool
    handled() const
    {
        return moved || response;
    }
};

}  // namespace ripple

#endif
