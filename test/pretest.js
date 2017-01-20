var fs = require('fs');
var path = require('path');
var joinPath = path.join.bind(path, __dirname);

fs.readdirSync(joinPath('ripple-lib')).forEach(function(fileName) {
  var src_path = joinPath('ripple-lib', fileName);
  var dst_path = joinPath('../node_modules/ripple-lib/dist/npm/core/', fileName);

  console.log(src_path + ' > ' + dst_path);

  fs.writeFileSync(dst_path, fs.readFileSync(src_path));
});
