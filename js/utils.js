// YYY Should probably have two versions: node vs browser

var fs = require("fs");
var path = require("path");

Function.prototype.method = function(name,func) {
  this.prototype[name] = func;

  return this;
};

var filterErr = function(code, done) {
  return function(e) {
      done(e.code !== code ? e : undefined);
    };
};

var throwErr = function(done) {
  return function(e) {
      if (e)
	throw e;
      
      done();
    };
};
 
// apply function to elements of array. Return first true value to done or undefined.
var mapOr = function(func, array, done) {
  if (array.length) {
    func(array[array.length-1], function(v) {
	if (v) {
	  done(v);
	}
	else {
	  array.length -= 1;
	  mapOr(func, array, done);
	}
      });
  }
  else {
    done();
  }
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

// Empty a directory.
var emptyPath = function(dirPath, done) {
  fs.readdir(dirPath, function(err, files) {
    if (err) {
      done(err);
    }
    else {
      mapOr(rmPath, files.map(function(f) { return path.join(dirPath, f); }), done);
    }
  });
};

// Remove path recursively.
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

var trace = function(comment, func) {
  return function() {
      console.log("%s: %s", trace, arguments.toString);
      func(arguments);
    };
};

var hexToString = function (h) {
  var	a = [];
  var	i = 0;

  if (h.length % 2) {
    a.push(String.fromCharCode(parseInt(h.substring(0, 1), 16)));
    i = 1;
  }

  for (; i != h.length; i += 2) {
    a.push(String.fromCharCode(parseInt(h.substring(i, i+2), 16)));
  }
  
  return a.join("");
};

var stringToHex = function (s) {
  return Array.prototype.map.call(s, function (c) {
      var b = c.charCodeAt(0);

      return b < 16 ? "0" + b.toString(16) : b.toString(16);
    }).join("");
};

exports.emptyPath   = emptyPath;
exports.mapOr	    = mapOr;
exports.mkPath	    = mkPath;
exports.resetPath   = resetPath;
exports.rmPath	    = rmPath;
exports.trace	    = trace;
exports.hexToString = hexToString;
exports.stringToHex = stringToHex;

// vim:sw=2:sts=2:ts=8
