# NPM tests

## Basic usage.

Documentation for running these tests can be found with the
[_Ripple build instructions_][unit_testing].
(Also for [_Windows_][windows_unit_testing],
[_OS X_][osx_unit_testing],
or [_Ubunutu_][ubuntu_unit_testing].)

## Advanced usage.

### Debugging rippled

By default, each test will start and stop an independent instance of rippled.
This ensures that each test is run against the known
[_genesis ledger_][genesis_ledger].

To use a running rippled, particularly one running in a debugger, follow
these steps:

1. If you haven't already, copy the example configuration file:
`cp test/config-example.js test/config.js`
2. Edit test/config.js to specify a "debug" server configuration.
  1. Change the existing default server: `exports.server_default = "debug";`
  (near the top of the file).
  2. Add a "debug" server configuration based on the default "alpha" server
  configuration, near the end of the file.
  ```
  exports.servers.debug = extend({
    no_server: true,
    debug_logfile: "debug.log"
  }, exports.servers.alpha);
  ```
3. Create a rippled.cfg file for the tests.
  1. Run `npm test`. The tests will fail. **That is ok and expected.**
  2. Copy and/or rename the *tmp/server/debug/rippled.cfg* file to somewhere 
  convenient.
4. Start rippled in your debugger of choice with command line options
`-a -v --conf <your npm rippled.cfg>`. Set any breakpoints now.
5. To skip any tests that you don't want to debug, especially if they modify the
ledger, change `test(` to `test.skip(` in the .js file.
6. Start your test(s) in the node-inspector debugger.
(Note that the tests can be run without the debugger, but you will probably
have problems with time outs or reused ledgers. Use at your own risk.)
  1. `node_modules/node-inspector/bin/inspector.js`
  2. `mocha --debug --debug-brk test/<testfile.js>`
  3. Browse to http://127.0.0.1:8080/debug?port=5858 in a browser supported 
  by node-inspector (i.e. Chrome or Safari).
7. If you would like to run multiple tests, you probably want to put
a breakpoint in the following function:
  * File testutils.js -> function `build_teardown()` -> nested function
  `teardown()` -> nested series function `stop_server()`: When this is
  hit, stop and restart rippled.

### After debugging

1. To return to the default behavior, edit test/config.js and change the
default server back to its original value. `exports.server_default = "alpha";`. You can leave the "debug" server configuration for future use.

[unit_testing]: https://wiki.ripple.com/Rippled_build_instructions#node-inspector
[windows_unit_testing]: https://wiki.ripple.com/Visual_Studio_2013_Build_Instructions#Unit_Tests_.28Recommended.29
[osx_unit_testing]: https://wiki.ripple.com/OSX_Build_Instructions#System_Tests_.28Recommended.29
[ubuntu_unit_testing]: https://wiki.ripple.com/Ubuntu_build_instructions#System_Tests_.28Recommended.29
[genesis_ledger]: https://wiki.ripple.com/Genesis_ledger
