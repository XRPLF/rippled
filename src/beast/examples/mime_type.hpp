//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_HTTP_MIME_TYPE_H_INCLUDED
#define BEAST_EXAMPLE_HTTP_MIME_TYPE_H_INCLUDED

#include <string>
#include <boost/filesystem/path.hpp>

namespace beast {
namespace http {

// Return the Mime-Type for a given file extension
template<class = void>
std::string
mime_type(std::string const& path)
{
    auto const ext =
        boost::filesystem::path{path}.extension().string();
    if(ext == ".txt")  return "text/plain";
    if(ext == ".htm")  return "text/html";
    if(ext == ".html") return "text/html";
    if(ext == ".php")  return "text/html";
    if(ext == ".css")  return "text/css";
    if(ext == ".js")   return "application/javascript";
    if(ext == ".json") return "application/json";
    if(ext == ".xml")  return "application/xml";
    if(ext == ".swf")  return "application/x-shockwave-flash";
    if(ext == ".flv")  return "video/x-flv";
    if(ext == ".png")  return "image/png";
    if(ext == ".jpe")  return "image/jpeg";
    if(ext == ".jpeg") return "image/jpeg";
    if(ext == ".jpg")  return "image/jpeg";
    if(ext == ".gif")  return "image/gif";
    if(ext == ".bmp")  return "image/bmp";
    if(ext == ".ico")  return "image/vnd.microsoft.icon";
    if(ext == ".tiff") return "image/tiff";
    if(ext == ".tif")  return "image/tiff";
    if(ext == ".svg")  return "image/svg+xml";
    if(ext == ".svgz") return "image/svg+xml";
    return "application/text";
}

} // http
} // beast

#endif
