Simple JSON-RPC
================

It is really simple JSON-RPC :)
Now, only client but server coming soon...

Installation
------------

Simply use npm to install it

    npm install simple-jsonrpc

or download the code from the repo and stick it in your project folder.

Simple usage :)
------------

    var jsonrpc = require('simple-jsonrpc');
    var client = jsonrpc.client("https://my-json-rpc-server/path-to-something/");
    client.call('add', [1, 2], function (result) {
        console.log(result);
    });
