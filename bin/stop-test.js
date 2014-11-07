/* -------------------------------- REQUIRES -------------------------------- */

var child = require("child_process");
var assert = require("assert");

/* --------------------------------- CONFIG --------------------------------- */

if (process.argv[2] == null) {
  [
   'Usage: ',
   '',
   '  `node bin/stop-test.js i,j [rippled_path] [rippled_conf]`',
   '',
   '  Launch rippled and stop it after n seconds for all n in [i, j}',
   '  For all even values of n launch rippled with `--fg`',
   '  For values of n where n % 3 == 0 launch rippled with `--fg`\n',
   'Examples: ',
   '',
   '  $ node bin/stop-test.js 5,10',
   ('  $ node bin/stop-test.js 1,4 ' +
      'build/clang.debug/rippled $HOME/.confs/rippled.cfg')
   ]
      .forEach(function(l){console.log(l)});

  process.exit();
} else {
  var testRange = process.argv[2].split(',').map(Number);
  var rippledPath = process.argv[3] || 'build/rippled'
  var rippledConf = process.argv[4] || 'rippled.cfg'
}

var options = {
  env: process.env,
  stdio: 'ignore' // we could dump the child io when it fails abnormally
};

// default args
var conf_args = ['--conf='+rippledConf];
var start_args  = conf_args.concat([/*'--net'*/])
var stop_args = conf_args.concat(['stop']);

/* --------------------------------- HELPERS -------------------------------- */

function start(args) {
    return child.spawn(rippledPath, args, options);
}
function stop(rippled) { child.execFile(rippledPath, stop_args, options)}
function secs_l8r(ms, f) {setTimeout(f, ms * 1000); }

function show_results_and_exit(results) {
  console.log(JSON.stringify(results, undefined, 2));
  process.exit();
}

var timeTakes = function (range) {
  function sumRange(n) {return (n+1) * n /2}
  var ret = sumRange(range[1]);
  if (range[0] > 1) {
    ret = ret - sumRange(range[0] - 1)
  }
  var stopping = (range[1] - range[0]) * 0.5;
  return ret + stopping;
}

/* ---------------------------------- TEST ---------------------------------- */

console.log("Test will take ~%s seconds", timeTakes(testRange));

(function oneTest(n /* seconds */, results) {
  if (n >= testRange[1]) {
    // show_results_and_exit(results);
    console.log(JSON.stringify(results, undefined, 2));
    oneTest(testRange[0], []);
    return;
  }

  var args = start_args;
  if (n % 2 == 0) {args = args.concat(['--fg'])}
  if (n % 3 == 0) {args = args.concat(['--net'])}

  var result = {args: args, alive_for: n};
  results.push(result);

  console.log("\nLaunching `%s` with `%s` for %d seconds",
                rippledPath, JSON.stringify(args), n);

  rippled = start(args);
  console.log("Rippled pid: %d", rippled.pid);

  // defaults
  var b4StopSent = false;
  var stopSent = false;
  var stop_took = null;

  rippled.once('exit', function(){
    if (!stopSent && !b4StopSent) {
      console.warn('\nRippled exited itself b4 stop issued');
      process.exit();
    };

    // The io handles close AFTER exit, may have implications for
    // `stdio:'inherit'` option to `child.spawn`.
    rippled.once('close', function() {
      result.stop_took = (+new Date() - stop_took) / 1000; // seconds
      console.log("Stopping after %d seconds took %s seconds",
                   n, result.stop_took);
      oneTest(n+1, results);
    });
  });

  secs_l8r(n, function(){
    console.log("Stopping rippled after %d seconds", n);

    // possible race here ?
    // seems highly unlikely, but I was having issues at one point
    b4StopSent=true;
    stop_took = (+new Date());
    // when does `exit` actually get sent?
    stop();
    stopSent=true;

    // Sometimes we want to attach with a debugger.
    if (process.env.ABORT_TESTS_ON_STALL != null) {
      // We wait 30 seconds, and if it hasn't stopped, we abort the process
      secs_l8r(30, function() {
        if (result.stop_took == null) {
          console.log("rippled has stalled");
          process.exit();
        };
      });
    }
  })
}(testRange[0], []));