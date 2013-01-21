var net = require("net"),
    domains = ["*:*"]; // Domain:Port

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
