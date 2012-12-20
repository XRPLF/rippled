module.exports = function(grunt) {
  grunt.initConfig({
    pkg: '<json:package.json',
    meta: {
      banner: '/*! <%= pkg.name %> - v<%= pkg.version %> - ' +
        '<%= grunt.template.today("yyyy-mm-dd") %>\n' +
        '<%= pkg.homepage ? "* " + pkg.homepage + "\n" : "" %>' +
        '* Copyright (c) <%= grunt.template.today("yyyy") %> <%= pkg.author.name %>;' +
        ' Licensed <%= _.pluck(pkg.licenses, "type").join(", ") %> */'
    },
    concat: {
      sjcl: {
        src: [
          "src/js/sjcl/core/sjcl.js",
//          "src/js/sjcl/core/aes.js",
          "src/js/sjcl/core/bitArray.js",
          "src/js/sjcl/core/codecString.js",
          "src/js/sjcl/core/codecHex.js",
          "src/js/sjcl/core/codecBase64.js",
          "src/js/sjcl/core/codecBytes.js",
          "src/js/sjcl/core/sha256.js",
          "src/js/sjcl/core/sha1.js",
//          "src/js/sjcl/core/ccm.js",
//          "src/js/sjcl/core/cbc.js",
//          "src/js/sjcl/core/ocb2.js",
//          "src/js/sjcl/core/hmac.js",
//          "src/js/sjcl/core/pbkdf2.js",
          "src/js/sjcl/core/random.js",
          "src/js/sjcl/core/convenience.js",
          "src/js/sjcl/core/bn.js",
          "src/js/sjcl/core/ecc.js",
          "src/js/sjcl/core/srp.js"
        ],
        dest: 'build/sjcl.js'
      }
    },
  });

  // Tasks
  grunt.registerTask('default', 'concat:sjcl');
};
