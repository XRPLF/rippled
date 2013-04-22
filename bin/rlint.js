#!/usr/bin/node

var async       = require('async');
var Remote      = require('../src/js/remote').Remote;
var Transaction = require('../src/js/transaction').Transaction;
var UInt160     = require('../src/js/uint160').UInt160;
var Amount      = require('../src/js/amount').Amount;

var book_key = function (book) {
  return book.taker_pays.currency
    + ":" + book.taker_pays.issuer
    + ":" + book.taker_gets.currency
    + ":" + book.taker_gets.issuer;
};

var book_key_cross = function (book) {
  return book.taker_gets.currency
    + ":" + book.taker_gets.issuer
    + ":" + book.taker_pays.currency
    + ":" + book.taker_pays.issuer;
};

var ledger_verify = function (ledger) {
  var dir_nodes = ledger.accountState.filter(function (entry) {
      return entry.LedgerEntryType === 'DirectoryNode'    // Only directories
        && entry.index === entry.RootIndex                // Only root nodes
        && 'TakerGetsCurrency' in entry;                  // Only offer directories
    });

  var books = {};

  dir_nodes.forEach(function (node) {
      var book = {
        taker_gets: {
            currency: UInt160.from_generic(node.TakerGetsCurrency).to_json(),
            issuer: UInt160.from_generic(node.TakerGetsIssuer).to_json()
          },
        taker_pays: {
          currency: UInt160.from_generic(node.TakerPaysCurrency).to_json(),
          issuer: UInt160.from_generic(node.TakerPaysIssuer).to_json()
        },
        quality: Amount.from_quality(node.RootIndex)
      };

      books[book_key(book)] = book;

//      console.log(JSON.stringify(node, undefined, 2));
    });

//  console.log(JSON.stringify(dir_entry, undefined, 2));
  console.log("#%s books: %s", ledger.ledger_index, Object.keys(books).length);

  Object.keys(books).forEach(function (key) {
      var book        = books[key];
      var key_cross   = book_key_cross(book);
      var book_cross  = books[key_cross];

      if (book && book_cross && !book_cross.done)
      {
        var book_cross_quality_inverted = Amount.from_json("1.0/1/1").divide(book_cross.quality);

        if (book_cross_quality_inverted.compareTo(book.quality) > 0)
        {
          console.log("crossing: #%s :: %s :: %s :: %s", ledger.ledger_index, key, book.quality.to_text(), book_cross.quality.to_text());
        }

        book_cross.done = true;
      }
    });
};

var ledger_request = function (remote, ledger_index, done) {
 remote.request_ledger(undefined, {
      accounts: true,
      expand: true,
    })
  .ledger_index(ledger_index)
  .on('success', function (m) {
      // console.log("ledger: ", ledger_index);
      // console.log("ledger: ", JSON.stringify(m, undefined, 2));
      done(m.ledger);
    })
  .on('error', function (m) {
      console.log("error");
      done();
    })
  .request();
};

var usage = function () {
  console.log("rlint.js _websocket_ip_ _websocket_port_ ");
};

var finish = function (remote) {
  remote.disconnect();

  // XXX Because remote.disconnect() doesn't work:
  process.exit();
};

console.log("args: ", process.argv.length);
console.log("args: ", process.argv);

if (process.argv.length < 4) {
  usage();
}
else {
  var remote  = Remote.from_config({
        websocket_ip:   process.argv[2],
        websocket_port: process.argv[3],
      })
    .once('ledger_closed', function (m) {
        console.log("ledger_closed: ", JSON.stringify(m, undefined, 2));

        if (process.argv.length === 5) {
          var ledger_index  = process.argv[4];

          ledger_request(remote, ledger_index, function (l) {
              if (l) {
                ledger_verify(l);
              }

              finish(remote);
            });

        } else if (process.argv.length === 6) {
          var ledger_start  = Number(process.argv[4]);
          var ledger_end    = Number(process.argv[5]);
          var ledger_cursor = ledger_end;

          async.whilst(
            function () {
              return ledger_start <= ledger_cursor && ledger_cursor <=ledger_end;
            },
            function (callback) {
              // console.log(ledger_cursor);

              --ledger_cursor;

              ledger_request(remote, ledger_cursor, function (l) {
                  if (l) {
                    ledger_verify(l);
                  }

                  callback();
                });
            },
            function (error) {
              finish(remote);
            });

        } else {
          finish(remote);
        }
      })
    .connect();
}

// vim:sw=2:sts=2:ts=8:et
