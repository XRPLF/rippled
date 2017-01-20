# Integration tests

## Basic usage.

Documentation for installation of dependencies and running these
tests can be found with the
[_Rippled build instructions_][unit_testing].
(Also for [_Windows_][windows_unit_testing],
[_OS X_][osx_unit_testing],
or [_Ubuntu_][ubuntu_unit_testing].)

## Advanced usage.

These instructions assume familiarity with the instructions linked above.

### Debugging rippled

By default, each test will start and stop an independent instance of `rippled`.
This ensures that each test is run against the known
[_genesis ledger_][genesis_ledger].

To use a running `rippled`, particularly one running in a debugger, follow
these steps:

#### Setup

##### Using configuration files

1. Make a copy of the example configuration file: `cp -i test/config-example.js test/config.js`

2. Edit `test/config.js` to select the "debug" server configuration.
  * Change the existing default server to: `exports.server_default = "debug";`
  (near the top of the file).

3. Create a `rippled.cfg` file for the tests.
  1. Run `npm test`. The tests will fail. **This failure is expected.**
  2. Copy and/or rename the `tmp/server/debug/rippled.cfg` file to somewhere
  convenient.

##### Using the command line

1. Create a `rippled.cfg` file for the tests.
  1. Run `npm test --noserver`. The tests will fail. **This failure is expected.**
  2. Copy and/or rename the `tmp/server/alpha/rippled.cfg` file to somewhere
  convenient.

#### Running the tests.

1. Start `rippled` (in a debugger) with command line options
`-av --conf <rippled-created-above.cfg>`.

2. Set any desired breakpoints in the `rippled` source.

3. Running one test per [_genesis ledger_][genesis_ledger] is highly recommended.
If the relevant `.js` file contains more than one test, change `test(` to
`test.only(` for the single desired test.
  * To run multiple tests, change `test(` to `test.skip(` for any undesired tests
  in the .js file.

4. Start test(s) in the [_node-inspector_][node_inspector] debugger.
(The tests can be run without the debugger, but there will probably
be problems with timeouts or reused ledgers).
  1. `node_modules/node-inspector/bin/inspector.js &`
  2. `node node_modules/.bin/mocha --debug --debug-brk test/<testfile.js>`
  3. Browse to http://127.0.0.1:8080/debug?port=5858 in a browser supported
  by [_node-inspector_][node_inspector] (i.e. Chrome or Safari).

5. To run multiple tests (not recommended), put a breakpoint in the following function:
  * File `testutils.js` -> function `build_teardown()` -> nested function
  `teardown()` -> nested series function `stop_server()`.
    * When this breakpoint is hit, stop and restart `rippled`.

6. Use the [_node-inspector UI_][node_inspector_ui] to step through and run
the test(s) until control is handed off to `rippled`. When the request is
finished control will be handed back to node-inspector, which may or may not
stop depending on which breakpoints are set.

### After debugging using configuration files.

With the command line `--noserver` flag, this step is unnecessary.

1. To return to the default behavior, edit `test/config.js` and change the
default server back to its original value: `exports.server_default = "alpha";`.
  * Alternately, delete `test/config.js`.

[unit_testing]: https://wiki.ripple.com/Rippled_build_instructions#Unit_testing
[windows_unit_testing]: https://wiki.ripple.com/Visual_Studio_2013_Build_Instructions#Unit_Tests_.28Recommended.29
[osx_unit_testing]: https://wiki.ripple.com/OSX_Build_Instructions#System_Tests_.28Recommended.29
[ubuntu_unit_testing]: https://wiki.ripple.com/Ubuntu_build_instructions#System_Tests_.28Recommended.29
[genesis_ledger]: https://wiki.ripple.com/Genesis_ledger
[node_inspector]: https://wiki.ripple.com/Rippled_build_instructions#node-inspector
[node_inspector_ui]: https://github.com/node-inspector/node-inspector/blob/master/README.md
