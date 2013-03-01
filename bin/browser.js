#!/usr/bin/node

var http      = require("http");

var Remote    = require("../src/js/remote.js").Remote;

var program   = process.argv[1];

if (process.argv.length < 4 || process.argv.length > 7) {
  console.log("Usage: %s ws_ip ws_port [<ip> [<port> [<start>]]]", program);
}
else {
  var ws_ip   = process.argv[2];
  var ws_port = process.argv[3];
  var ip      = process.argv.length > 4 ? process.argv[4] : "127.0.0.1";
  var port    = process.argv.length > 5 ? process.argv[5] : "8080";

console.log("START");
  var self  = this;
  var remote  = (new Remote({
                    websocket_ip: ws_ip,
                    websocket_port: ws_port,
                    trace: true
                  }))
                  .on('state', function (m) {
                      console.log("STATE: %s", m);

                      self.state   = m;
                    })
//                  .once('ledger_closed', callback)
                  .connect()
                  ;

console.log("SERVE");
  var server  = http.createServer(function (req, res) {
      var input = "";

      req.setEncoding();

      req.on('data', function (buffer) {
          // console.log("DATA: %s", buffer);
          input = input + buffer;
        });

      req.on('end', function () {
          console.log("URL: %s", req.url);
          console.log("HEADERS: %s", JSON.stringify(req.headers, undefined, 2));

          if (req.url === "/ledger_header") {

              var request = remote
                .request_ledger_header()
                .ledger_index(-1)
                .on('success', function (m) {
                    console.log("Ledger: %s", JSON.stringify(m, undefined, 2));

                    res.statusCode = 200;
                    res.end(
                      "<HTML>"
                        + "<HEAD><TITLE>Title</TITLE></HEAD>"
                        + "<BODY BACKGROUND=\"#FFFFFF\">"
                        + "State: " + self.state
                        + "<UL>"
                        + "<LI><A HREF=\"/\">home</A>"
                        + "<LI><A HREF=\"/ledger_header\">ledger_header</A>"
                        + "</UL>"
                        + "<PRE>"
                        + JSON.stringify(m, undefined, 2)
                        + "</PRE>"
                        + "</BODY>"
                        + "</HTML>"
                      );
                  })
                .request();
          }
          else {
            res.statusCode = req.url === "/" ? 200 : 400;
            res.end(
              "<HTML>"
                + "<HEAD><TITLE>Title</TITLE></HEAD>"
                + "<BODY BACKGROUND=\"#FFFFFF\">"
                + "State: " + self.state
                + "<UL>"
                + "<LI><A HREF=\"/\">home</A>"
                + "<LI><A HREF=\"/ledger_header\">ledger_header</A>"
                + "</UL>"
                + "</BODY>"
                + "</HTML>"
              );
          }
        });
    });

  server.listen(port, ip, undefined,
    function () {
      console.log("Listening at: http://%s:%s", ip, port);
    });
}

// vim:sw=2:sts=2:ts=8:et
