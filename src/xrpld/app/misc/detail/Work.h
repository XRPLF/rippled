#ifndef XRPL_APP_MISC_DETAIL_WORK_H_INCLUDED
#define XRPL_APP_MISC_DETAIL_WORK_H_INCLUDED

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>

namespace ripple {

namespace detail {

using response_type =
    boost::beast::http::response<boost::beast::http::string_body>;

class Work
{
public:
    virtual ~Work() = default;

    virtual void
    run() = 0;

    virtual void
    cancel() = 0;
};

}  // namespace detail

}  // namespace ripple

#endif
