#!/usr/bin/node
//
// This program allows IE 9 ripple-clients to make websocket connections to
// rippled using flash.  As IE 9 does not have websocket support, this required
// if you wish to support IE 9 ripple-clients.
//
// http://www.lightsphere.com/dev/articles/flash_socket_policy.html
//
// For better security, be sure to set the Port below to the port of your
// [websocket_public_port].
//

var net	    = require("net"),
    port    = "*",
    domains = ["*:"+port]; // Domain:Port

net.createServer(
  function(socket) {
    socket.write("<?xml version='1.0' ?>\n");
    socket.write("<!DOCTYPE cross-domain-policy SYSTEM 'http://www.macromedia.com/xml/dtds/cross-domain-policy.dtd'>\n");
    socket.write("<cross-domain-policy>\n");
    domains.forEach(
      function(domain) {
        var parts = domain.split(':');
        socket.write("\t<allow-access-from domain='" + parts[0] + "' to-ports='" + parts[1] + "' />\n");
      }
    );
    socket.write("</cross-domain-policy>\n");
    socket.end();
  }
).listen(843);
