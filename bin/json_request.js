#!/usr/bin/node
//
// This is a tool to issue JSON-RPC requests from the command line.
//
// This can be used to test a JSON-RPC server.
//
// Requires: npm simple-jsonrpc
//

var jsonrpc   = require('simple-jsonrpc');

var program   = process.argv[1];

if (5 !== process.argv.length) {
  console.log("Usage: %s <URL> <method> <json>", program);
}
else {
  var url       = process.argv[2];
  var method    = process.argv[3];
  var json_raw  = process.argv[4];
  var json;

  try {
    json      = JSON.parse(json_raw);
  }
  catch (e) {
      console.log("JSON parse error: %s", e.message);
      throw e;
  }

  var client  = jsonrpc.client(url);

  client.call(method, json,
    function (result) {
      console.log(JSON.stringify(result, undefined, 2));
    },
    function (error) {
      console.log(JSON.stringify(error, undefined, 2));
    });
}

// vim:sw=2:sts=2:ts=8:et
