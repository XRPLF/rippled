#!/usr/bin/node
//
// ledger?l=L
// transaction?h=H
// ledger_entry?l=L&h=H
// account?l=L&a=A
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

var async     = require("async");
var extend    = require("extend");
var http      = require("http");
var url       = require("url");

var Remote    = require("ripple-lib").Remote;

var program   = process.argv[1];

var httpd_response = function (res, opts) {
  var self=this;

  res.statusCode = opts.statusCode;
  res.end(
    "<HTML>"
      + "<HEAD><TITLE>Title</TITLE></HEAD>"
      + "<BODY BACKGROUND=\"#FFFFFF\">"
      + "State:" + self.state
      + "<UL>"
      + "<LI><A HREF=\"/\">home</A>"
      + "<LI>" + html_link('r4EM4gBQfr1QgQLXSPF4r7h84qE9mb6iCC')
//      + "<LI><A HREF=\""+test+"\">rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh</A>"
      + "<LI><A HREF=\"/ledger\">ledger</A>"
      + "</UL>"
      + (opts.body || '')
      + '<HR><PRE>'
      + (opts.url || '')
      + '</PRE>'
      + "</BODY>"
      + "</HTML>"
    );
};

var html_link = function (generic) {
  return '<A HREF="' + build_uri({ type: 'account', account: generic}) + '">' + generic + '</A>';
};

// Build a link to a type.
var build_uri = function (params, opts) {
  var c;

  if (params.type === 'account') {
    c = {
        pathname: 'account',
        query: {
          l: params.ledger,
          a: params.account,
        },
      };

  } else if (params.type === 'ledger') {
    c = {
        pathname: 'ledger',
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

  opts  = opts || {};

  c.protocol  = "http";
  c.hostname  = opts.hostname || self.base.hostname;
  c.port      = opts.port || self.base.port;

  return url.format(c);
};

var build_link = function (item, link) {
console.log(link);
  return "<A HREF=" + link + ">" + item + "</A>";
};

var rewrite_field = function (type, obj, field, opts) {
  if (field in obj) {
    obj[field]  = rewrite_type(type, obj[field], opts);
  }
};

var rewrite_type = function (type, obj, opts) {
  if ('amount' === type) {
    if ('string' === typeof obj) {
      // XRP.
      return '<B>' + obj + '</B>';

    } else {
      rewrite_field('address', obj, 'issuer', opts);

      return obj; 
    }
    return build_link(
      obj,
      build_uri({
          type: 'account',
          account: obj
        }, opts)
    );
  }
  if ('address' === type) {
    return build_link(
      obj,
      build_uri({
          type: 'account',
          account: obj
        }, opts)
    );
  }
  else if ('ledger' === type) {
    return build_link(
      obj,
      build_uri({
          type: 'ledger',
          ledger: obj,
        }, opts)
      );
  }
  else if ('node' === type) {
    // A node
    if ('PreviousTxnID' in obj)
      obj.PreviousTxnID      = rewrite_type('transaction', obj.PreviousTxnID, opts);

    if ('Offer' === obj.LedgerEntryType) {
      if ('NewFields' in obj) {
        if ('TakerGets' in obj.NewFields)
          obj.NewFields.TakerGets = rewrite_type('amount', obj.NewFields.TakerGets, opts);

        if ('TakerPays' in obj.NewFields)
          obj.NewFields.TakerPays = rewrite_type('amount', obj.NewFields.TakerPays, opts);
      }
    }

    obj.LedgerEntryType  = '<B>' + obj.LedgerEntryType + '</B>';

    return obj;
  }
  else if ('transaction' === type) {
    // Reference to a transaction.
    return build_link(
      obj,
      build_uri({
          type: 'transaction',
          hash: obj
        }, opts)
      );
  }

  return 'ERROR: ' + type;
};

var rewrite_object = function (obj, opts) {
  var out = extend({}, obj);

  rewrite_field('address', out, 'Account', opts);

  rewrite_field('ledger', out, 'parent_hash', opts);
  rewrite_field('ledger', out, 'ledger_index', opts);
  rewrite_field('ledger', out, 'ledger_current_index', opts);
  rewrite_field('ledger', out, 'ledger_hash', opts);

  if ('ledger' in obj) {
    // It's a ledger header.
    out.ledger  = rewrite_object(out.ledger, opts);

    if ('ledger_hash' in out.ledger)
      out.ledger.ledger_hash = '<B>' + out.ledger.ledger_hash + '</B>';

    delete out.ledger.hash;
    delete out.ledger.totalCoins;
  }

  if ('TransactionType' in obj) {
    // It's a transaction.
    out.TransactionType = '<B>' + obj.TransactionType + '</B>';

    rewrite_field('amount', out, 'TakerGets', opts);
    rewrite_field('amount', out, 'TakerPays', opts);
    rewrite_field('ledger', out, 'inLedger', opts);

    out.meta.AffectedNodes = out.meta.AffectedNodes.map(function (node) {
        var kind  = 'CreatedNode' in node
          ? 'CreatedNode'
          : 'ModifiedNode' in node
            ? 'ModifiedNode'
            : 'DeletedNode' in node
              ? 'DeletedNode'
              : undefined;
        
        if (kind) {
          node[kind]  = rewrite_type('node', node[kind], opts);
        }
        return node;
      });
  }
  else if ('node' in obj && 'LedgerEntryType' in obj.node) {
    // Its a ledger entry.

    if (obj.node.LedgerEntryType === 'AccountRoot') {
      rewrite_field('address', out.node, 'Account', opts);
      rewrite_field('transaction', out.node, 'PreviousTxnID', opts);
      rewrite_field('ledger', out.node, 'PreviousTxnLgrSeq', opts);
    }

    out.node.LedgerEntryType = '<B>' + out.node.LedgerEntryType + '</B>';
  }

  return out;
};

var augment_object = function (obj, opts, done) {
  if (obj.node.LedgerEntryType == 'AccountRoot') {
    var   tx_hash   = obj.node.PreviousTxnID;
    var   tx_ledger = obj.node.PreviousTxnLgrSeq;

    obj.history                 = [];

    async.whilst(
      function () { return tx_hash; },
      function (callback) {
// console.log("augment_object: request: %s %s", tx_hash, tx_ledger);
        opts.remote.request_tx(tx_hash)
          .on('success', function (m) {
              tx_hash   = undefined;
              tx_ledger = undefined;

//console.log("augment_object: ", JSON.stringify(m));
              m.meta.AffectedNodes.filter(function(n) {
// console.log("augment_object: ", JSON.stringify(n));
// if (n.ModifiedNode)
// console.log("augment_object: %s %s %s %s %s %s/%s", 'ModifiedNode' in n, n.ModifiedNode && (n.ModifiedNode.LedgerEntryType === 'AccountRoot'), n.ModifiedNode && n.ModifiedNode.FinalFields && (n.ModifiedNode.FinalFields.Account === obj.node.Account), Object.keys(n)[0], n.ModifiedNode && (n.ModifiedNode.LedgerEntryType), obj.node.Account, n.ModifiedNode && n.ModifiedNode.FinalFields && n.ModifiedNode.FinalFields.Account);
// if ('ModifiedNode' in n && n.ModifiedNode.LedgerEntryType === 'AccountRoot')
// {
//   console.log("***: ", JSON.stringify(m));
//   console.log("***: ", JSON.stringify(n));
// }
                  return 'ModifiedNode' in n
                    && n.ModifiedNode.LedgerEntryType === 'AccountRoot'
                    && n.ModifiedNode.FinalFields
                    && n.ModifiedNode.FinalFields.Account === obj.node.Account;
                })
              .forEach(function (n) {
                  tx_hash   = n.ModifiedNode.PreviousTxnID;
                  tx_ledger = n.ModifiedNode.PreviousTxnLgrSeq;

                  obj.history.push({
                      tx_hash:    tx_hash,
                      tx_ledger:  tx_ledger
                    });
console.log("augment_object: next: %s %s", tx_hash, tx_ledger);
                });

              callback();
            })
          .on('error', function (m) {
              callback(m);
            })
          .request();
      },
      function (err) {
        if (err) {
          done();
        }
        else {
          async.forEach(obj.history, function (o, callback) {
              opts.remote.request_account_info(obj.node.Account)
                .ledger_index(o.tx_ledger)
                .on('success', function (m) {
//console.log("augment_object: ", JSON.stringify(m));
                    o.Balance       = m.account_data.Balance;
//                    o.account_data  = m.account_data;
                    callback();
                  })
                .on('error', function (m) {
                    o.error = m;
                    callback();
                  })
                .request();
            },
            function (err) {
              done(err);
            });
        }
      });
  }
  else {
    done();
  }
};

if (process.argv.length < 4 || process.argv.length > 7) {
  console.log("Usage: %s ws_ip ws_port [<ip> [<port> [<start>]]]", program);
}
else {
  var ws_ip   = process.argv[2];
  var ws_port = process.argv[3];
  var ip      = process.argv.length > 4 ? process.argv[4] : "127.0.0.1";
  var port    = process.argv.length > 5 ? process.argv[5] : "8080";

// console.log("START");
  var self  = this;
  
  var remote  = (new Remote({
                    websocket_ip: ws_ip,
                    websocket_port: ws_port,
                    trace: false
                  }))
                  .on('state', function (m) {
                      console.log("STATE: %s", m);

                      self.state   = m;
                    })
//                  .once('ledger_closed', callback)
                  .connect()
                  ;

  self.base = {
      hostname: ip,
      port:     port,
      remote:   remote,
    };

// console.log("SERVE");
  var server  = http.createServer(function (req, res) {
      var input = "";

      req.setEncoding();

      req.on('data', function (buffer) {
          // console.log("DATA: %s", buffer);
          input = input + buffer;
        });

      req.on('end', function () {
          // console.log("URL: %s", req.url);
          // console.log("HEADERS: %s", JSON.stringify(req.headers, undefined, 2));

          var _parsed = url.parse(req.url, true);
          var _url    = JSON.stringify(_parsed, undefined, 2);

          // console.log("HEADERS: %s", JSON.stringify(_parsed, undefined, 2));
          if (_parsed.pathname === "/account") {
              var request = remote
                .request_ledger_entry('account_root')
                .ledger_index(-1)
                .account_root(_parsed.query.a)
                .on('success', function (m) {
                    // console.log("account_root: %s", JSON.stringify(m, undefined, 2));

                    augment_object(m, self.base, function() {
                      httpd_response(res,
                          {
                            statusCode: 200,
                            url: _url,
                            body: "<PRE>"
                              + JSON.stringify(rewrite_object(m, self.base), undefined, 2)
                              + "</PRE>"
                          });
                    });
                  })
                .request();

          } else if (_parsed.pathname === "/ledger") {
            var request = remote
              .request_ledger(undefined, { expand: true, transactions: true })
              .on('success', function (m) {
                  // console.log("Ledger: %s", JSON.stringify(m, undefined, 2));

                  httpd_response(res,
                      {
                        statusCode: 200,
                        url: _url,
                        body: "<PRE>"
                          + JSON.stringify(rewrite_object(m, self.base), undefined, 2)
                          +"</PRE>"
                      });
                })

            if (_parsed.query.l && _parsed.query.l.length === 64) {
              request.ledger_hash(_parsed.query.l);
            }
            else if (_parsed.query.l) {
              request.ledger_index(Number(_parsed.query.l));
            }
            else {
              request.ledger_index(-1);
            }

            request.request();

          } else if (_parsed.pathname === "/transaction") {
              var request = remote
                .request_tx(_parsed.query.h)
//                .request_transaction_entry(_parsed.query.h)
//              .ledger_select(_parsed.query.l)
                .on('success', function (m) {
                    // console.log("transaction: %s", JSON.stringify(m, undefined, 2));

                    httpd_response(res,
                        {
                          statusCode: 200,
                          url: _url,
                          body: "<PRE>"
                            + JSON.stringify(rewrite_object(m, self.base), undefined, 2)
                            +"</PRE>"
                        });
                  })
                .on('error', function (m) {
                    httpd_response(res,
                        {
                          statusCode: 200,
                          url: _url,
                          body: "<PRE>"
                            + 'ERROR: ' + JSON.stringify(m, undefined, 2)
                            +"</PRE>"
                        });
                  })
                .request();

          } else {
            var test  = build_uri({
                type: 'account',
                ledger: 'closed',
                account: 'rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh',
              }, self.base);

            httpd_response(res,
                {
                  statusCode: req.url === "/" ? 200 : 404,
                  url: _url,
                });
          }
        });
    });

  server.listen(port, ip, undefined,
    function () {
      console.log("Listening at: http://%s:%s", ip, port);
    });
}

// vim:sw=2:sts=2:ts=8:et
