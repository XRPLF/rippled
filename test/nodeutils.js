var async = require("async");
var fs	  = require("fs");
var path  = require("path");

var utils  = require("ripple-lib").utils;

// Empty a directory.
// done(err) : err = true if an error occured.
var emptyPath = function(dirPath, done) {
  fs.readdir(dirPath, function(err, files) {
    if (err) {
      done(err);
    }
    else {
      async.some(files.map(function(f) { return path.join(dirPath, f); }), rmPath, done);
    }
  });
};

// Make a directory and sub-directories.
var mkPath = function(dirPath, mode, done) {
  fs.mkdir(dirPath, typeof mode === "string" ? parseInt(mode, 8) : mode, function(e) {
      if (!e ||  e.code === "EEXIST") {
	// Created or already exists, done.
	done();
      }
      else if (e.code === "ENOENT") {
	// Missing sub dir.

	mkPath(path.dirname(dirPath), mode, function(e) {
	    if (e) {
	      throw e;
	    }
	    else {
	      mkPath(dirPath, mode, done);
	    }
	  });
      }
      else {
	throw e;
      }
  });
};

// Create directory if needed and empty if needed.
var resetPath = function(dirPath, mode, done) {
  mkPath(dirPath, mode, function(e) {
      if (e) {
	done(e);
      }
      else {
	emptyPath(dirPath, done);
      }
    });
};

// Remove path recursively.
// done(err)
var rmPath = function(dirPath, done) {
//  console.log("rmPath: %s", dirPath);

  fs.lstat(dirPath, function(err, stats) {
      if (err && err.code == "ENOENT") {
	done();
      }
      if (err) {
	done(err);
      }
      else if (stats.isDirectory()) {
	emptyPath(dirPath, function(e) {
	    if (e) {
	      done(e);
	    }
	    else {
//	      console.log("rmdir: %s", dirPath); done();
	      fs.rmdir(dirPath, done);
	    }
	  });
      }
      else {
//	console.log("unlink: %s", dirPath); done();
	fs.unlink(dirPath, done);
      }
    });
};

exports.mkPath	    = mkPath;
exports.resetPath   = resetPath;
exports.rmPath	    = rmPath;

// vim:sw=2:sts=2:ts=8:et
