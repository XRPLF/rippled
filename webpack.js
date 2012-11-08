var pkg = require('./package.json');
var webpack = require("webpack");
var async = require("async");
var extend = require("extend");

var programPath = __dirname + "/src/js/remote.js";

console.log('Compiling Ripple JavaScript...');
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
  library: 'ripple'
};
function build(opts) {
  var opts = extend({}, defaultOpts, opts);
  opts.output = __dirname + "/build/"+opts.filename;
  return function (callback) {
    var filename = opts.filename;
    webpack(programPath, opts, function (err, result) {
      console.log(' '+filename, result.hash, '['+result.modulesCount+']');
      callback(err);
    });
  }
}

async.series(builds.map(build), function (err) {
  if (err) {
    console.error(err);
  }
});
