var pkg = require('./package.json');
var webpack = require("webpack");
var async = require("async");
var extend = require("extend");

var programPath = __dirname + "/src/js/remote.js";

var cfg = {
  watch: false,
  outputDir: __dirname + "/build"
};
for (var i = 0, l = process.argv.length; i < l; i++) {
  var arg = process.argv[i];
  if (arg === '-w' || arg === '--watch') {
    cfg.watch = true;
  } else if (arg === '-o') {
    cfg.outputDir = process.argv[++i];
  }
};

var builds = [{
  filename: 'ripple-'+pkg.version+'.js',
},{
  filename: 'ripple-'+pkg.version+'-debug.js',
  debug: true
},{
  filename: 'ripple-'+pkg.version+'-min.js',
  minimize: true
}];

var defaultOpts = {
  // [sic] Yes, this is the spelling upstream.
  libary: 'ripple',
  // However, it's fixed in webpack 0.8, so we include the correct spelling too:
  library: 'ripple',
  watch: cfg.watch
};

function build(opts) {
  var opts = extend({}, defaultOpts, opts);
  opts.output = cfg.outputDir + "/"+opts.filename;
  return function (callback) {
    var filename = opts.filename;
    webpack(programPath, opts, function (err, result) {
      console.log(' '+filename, result.hash, '['+result.modulesCount+']');
      if ("function" === typeof callback) {
        callback(err);
      }
    });
  }
}

if (!cfg.watch) {
  console.log('Compiling Ripple JavaScript...');
  async.series(builds.map(build), function (err) {
    if (err) {
      console.error(err);
    }
  });
} else {
  console.log('Watching files for changes...');
  builds.map(build).forEach(function (build) {
    build();
  });
}
