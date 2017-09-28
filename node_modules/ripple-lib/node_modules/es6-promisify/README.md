# es6-promisify

Converts callback-based functions to Promise-based functions.

## Install

Install with [npm](https://npmjs.org/package/es6-promisify)

```bash
npm install --save es6-promisify
```

## Example

```js
"use strict";

// Declare variables
var promisify, fs, stat;

// Load modules
promisify = require("es6-promisify");
fs        = require("fs");

// Convert the stat function
stat = promisify(fs.stat);

// Now usable as a promise!
stat("example.txt").then(function (stats) {
    console.log("Got stats", stats);
}).catch(function (err) {
    console.error("Yikes!", err);
});
```

## Provide your own callback
```js
"use strict";

// Declare variables
var promisify, fs, stat;

// Load modules
promisify = require("es6-promisify");
fs        = require("fs");

// Convert the stat function, with a custom callback
stat = promisify(fs.stat, function (err, result) {
    if (err) {
        console.error(err);
        return this.reject("Could not stat file");
    }
    this.resolve(result);
});

stat("example.txt").then(function (stats) {
    console.log("Got stats", stats);
}).catch(function (err) {
    // err = "Could not stat file"
});
```

## Promisify methods
```js
"use strict";

// Declare variables
var promisify, redis, client;

// Load modules
promisify = require("es6-promisify");
redis     = require("redis").createClient(6379, "localhost");

// Create a promise-based version of send_command
client = promisify(redis.send_command.bind(redis));

// Send commands to redis and get a promise back
client("ping", []).then(function (pong) {
    console.log("Got", pong);
}).catch(function (err) {
    console.error("Unexpected error", err);
}).then(function () {
    redis.quit();
});
```

### Tests
Test with nodeunit
```bash
$ npm test
```

Published under the [MIT License](http://opensource.org/licenses/MIT).
