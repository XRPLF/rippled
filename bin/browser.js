#!/usr/bin/node
//
// ledger_header?l=L
// transaction?h=H
// ledger_entry?l=L&h=H
// account_root?l=L&a=A
// directory?l=L&dir_root=H&i=I
// directory?l=L&o=A&i=I     // owner directory
// offer?l=L&offer=H
// offer?l=L&account=A&i=I
// ripple_state=l=L&a=A&b=A&c=C
// account_lines?l=L&a=A
//
// A=address
// C=currency 3 letter code
// H=hash
// I=index
// L=current | closed | validated | index | hash
//

var extend    = require("extend");
var http      = require("http");
var url       = require("url");

var Remote    = require("../src/js/remote.js").Remote;

var program   = process.argv[1];

// Build a link to a type.
var build_uri = function (params, opts) {
  var c;

  if (params.type === 'account_root') {
    c = {
        pathname: 'account_root',
        query: {
          l: params.ledger,
          a: params.account,
        },
      };

  } else if (params.type === 'ledger_header') {
    c = {
        pathname: 'ledger_header',
        query: {
          l: params.ledger,
        },
      };

  } else if (params.type === 'transaction') {
    c = {
        pathname: 'transaction',
        query: {
          h: params.hash,
        },
      };
  } else {
    c = {};
  }

  c.protocol  = "http";
  c.hostname  = opts.hostname;
  c.port      = opts.port;

  return url.format(c);
};

var build_link = function (item, link) {
console.log(link);
  return "<A HREF=" + link + ">" + item + "</A>";
};

var rewrite_object = function (obj, opts) {
  var out = extend({}, obj);

  if ('ledger_index' in obj) {
    out.ledger_index  =
      build_link(
        obj.ledger_index,
        build_uri({
            type: 'ledger_header',
            ledger: obj.ledger_index,
          }, opts)
      );
  }

  if ('node' in obj) {
    if (obj.node.LedgerEntryType === 'AccountRoot') {
      out.node.PreviousTxnID  =
        build_link(
          obj.node.PreviousTxnID,
          build_uri({
              type: 'transaction',
              hash: obj.node.PreviousTxnID,
            }, opts)
        );
    }
  }

  return out;
};

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
  
  self.base = {
      hostname: ip,
      port:     port,
    };

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
          // console.log("HEADERS: %s", JSON.stringify(req.headers, undefined, 2));

          var _parsed = url.parse(req.url, true);
          var _url    = JSON.stringify(_parsed, undefined, 2);

          if (_parsed.pathname === "/account_root") {
              var request = remote
                .request_ledger_entry('account_root')
                .ledger_index(-1)
                .account_root(_parsed.query.a)
                .on('success', function (m) {
                    console.log("account_root: %s", JSON.stringify(m, undefined, 2));

                    res.statusCode = 200;
                    res.end(
                      "<HTML>"
                        + "<HEAD><TITLE>Title</TITLE></HEAD>"
                        + "<BODY BACKGROUND=\"#FFFFFF\">"
                        + "State: " + self.state
                        + "<UL>"
                        + "<LI><A HREF=\"/\">home</A>"
                        + "<LI><A HREF=\"/account_root\">account_root</A>"
                        + "</UL>"
                        + "<PRE>"
                        + JSON.stringify(rewrite_object(m, self.base), undefined, 2)
                        + "</PRE>"
                        + "</BODY>"
                        + "</HTML>"
                      );
                  })
                .request();

          } else if (_parsed.pathname === "/ledger_header") {
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

          } else if (_parsed.pathname === "/transaction") {
              var request = remote
                .request_transaction_entry(_parsed.query.h)
//              .ledger_select(_parsed.query.l)
                .on('success', function (m) {
                    console.log("transaction: %s", JSON.stringify(m, undefined, 2));

                    res.statusCode = 200;
                    res.end(
                      "<HTML>"
                        + "<HEAD><TITLE>Title</TITLE></HEAD>"
                        + "<BODY BACKGROUND=\"#FFFFFF\">"
                        + "State: " + self.state
                        + "<UL>"
                        + "<LI><A HREF=\"/\">home</A>"
                        + "<LI><A HREF=\"/account_root\">account_root</A>"
                        + "</UL>"
                        + "<PRE>"
                        + JSON.stringify(rewrite_object(m, self.base), undefined, 2)
                        + "</PRE>"
                        + "</BODY>"
                        + "</HTML>"
                      );
                  })
                .request();

          } else {
            var test  = build_uri({
                type: 'account_root',
                ledger: 'closed',
                account: 'rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh',
              }, self.base);

            res.statusCode = req.url === "/" ? 200 : 404;
            res.end(
              "<HTML>"
                + "<HEAD><TITLE>Title</TITLE></HEAD>"
                + "<BODY BACKGROUND=\"#FFFFFF\">"
                + "State: " + self.state
                + "<UL>"
                + "<LI><A HREF=\"/\">home</A>"
                + "<LI><A HREF=\""+test+"\">rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh</A>"
                + "<LI><A HREF=\"/ledger_header\">ledger_header</A>"
                + "</UL>"
                + "<PRE>"+_url+"</PRE>"
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
