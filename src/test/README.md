
# Unit Tests

## Running Tests

Unit tests are bundled in the `rippled` executable and can be executed using the
`--unittest` parameter. Without any arguments to this option, all non-manual
unit tests will be executed. If you want to run one or more manual tests, you
must specify it by suite or full-name (e.g. `ripple.app.NoRippleCheckLimits` or
just `NoRippleCheckLimits`).

More than one suite or group of suites can be specified as a comma separated
list via the argument. For example, `--unittest=beast,OversizeMeta` will run
all suites in the `beast` library (root identifier) as well as the test suite
named `OversizeMeta`). All name matches are case sensitive. 

Tests can be executed in parallel using several child processes by specifying
the `--unittest-jobs=N` parameter. The default behavior is to execute serially
using a single process.

The order that suites are executed is determined by the suite priority that 
is optionally specified when the suite is declared in the code with one of the
`BEAST_DEFINE_TESTSUITE` macros. By default, suites have a priority of 0, and
other suites can choose to declare an integer priority value to make themselves
execute before or after other suites based on their specified priority value.

By default, the framework will emit the name of each testcase/testsuite when it
starts and any messages sent to the suite `log` stream. The `--quiet` option will
suppress both types of messages, but combining `--unittest-log` with `--quiet`
will cause `log` messages to be emitted while suite/case names are suppressed.
