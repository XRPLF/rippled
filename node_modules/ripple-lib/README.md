#ripple-lib

A JavaScript API for interacting with Ripple in Node.js and the browser

[![Circle CI](https://circleci.com/gh/ripple/ripple-lib/tree/develop.svg?style=svg)](https://circleci.com/gh/ripple/ripple-lib/tree/develop) [![Coverage Status](https://coveralls.io/repos/ripple/ripple-lib/badge.png?branch=develop)](https://coveralls.io/r/ripple/ripple-lib?branch=develop)

[![NPM](https://nodei.co/npm/ripple-lib.png)](https://www.npmjs.org/package/ripple-lib)

###Features

+ Connect to a rippled server in JavaScript (Node.js or browser)
+ Issue [rippled API](https://ripple.com/build/rippled-apis/) requests
+ Listen to events on the Ripple network (transaction, ledger, etc.)
+ Sign and submit transactions to the Ripple network

###In this file

1. [Installation](#installation)
2. [Quick start](#quick-start)
3. [Running tests](#running-tests)

###Additional documentation

1. [Guides](docs/GUIDES.md)
2. [API Reference](docs/REFERENCE.md)
3. [Wiki](https://ripple.com/wiki/Ripple_JavaScript_library)

###Also see

+ [The Ripple wiki](https://ripple.com/wiki)
+ [ripple.com](https://ripple.com)

##Installation

**Via npm for Node.js**

```
  $ npm install ripple-lib
```

**Via bower (for browser use)**

```
  $ bower install ripple
```

See the [bower-ripple repo](https://github.com/ripple/bower-ripple) for additional bower instructions.


**Building ripple-lib for browser environments**

ripple-lib uses Gulp to generate browser builds. These steps will generate minified and non-minified builds of ripple-lib in the `build/` directory.

```
  $ git clone https://github.com/ripple/ripple-lib
  $ npm install
  $ npm run build
```

**Restricted browser builds**

You may generate browser builds that contain a subset of features. To do this, run `./node_modules/.bin/gulp build-<name>`

+ `build-core` Contains the functionality to make requests and listen for events such as `ledgerClose`. Only `ripple.Remote` is currently exposed. Advanced features like transaction submission and orderbook tracking are excluded from this build.

##Quick start

`Remote.js` ([remote.js](https://github.com/ripple/ripple-lib/blob/develop/src/js/ripple/remote.js)) is the point of entry for interacting with rippled

```js
/* Loading ripple-lib with Node.js */
var Remote = require('ripple-lib').Remote;

/* Loading ripple-lib in a webpage */
// var Remote = ripple.Remote;

var remote = new Remote({
  // see the API Reference for available options
  servers: [ 'wss://s1.ripple.com:443' ]
});

remote.connect(function() {
  /* remote connected */
  remote.requestServerInfo(function(err, info) {
    // process err and info
  });
});
```

##Running tests

1. Clone the repository

2. `cd` into the repository and install dependencies with `npm install`

3. `npm test`

**Generating code coverage**

ripple-lib uses `istanbul` to generate code coverage. To create a code coverage report, run `npm test --coverage`. The report will be created in `coverage/lcov-report/`.
