#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/Output.h>

#include <string>

namespace xrpl {

/**
 * Writes an HTTP reply carrying @p strMsg with status @p nStatus to @p output,
 * and logs the status at trace. The body is not logged here: it may carry a
 * credential this library cannot mask, so the caller logs it masked.
 *
 * A 401 with an empty body is answered with the fixed authentication page.
 *
 * @param nStatus The HTTP status code.
 * @param strMsg The body.
 * @param output Where the reply bytes are written.
 * @param j The journal the status is logged to.
 */
void
httpReply(int nStatus, std::string const& strMsg, json::Output const&, beast::Journal j);

}  // namespace xrpl
