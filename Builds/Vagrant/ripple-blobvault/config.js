// IP of the interface to bind to, default (null) means to bind to any
// exports.host = '127.0.0.1';
exports.host = null;

exports.blockscore = {
    key: ''
}

// Port to listen on
exports.port = 8080;

// Public URL for this blobvault (required for authinfo)
exports.url = "http://local.id.ripple.com";

// SSL settings
exports.ssl = false;

// Whether this blob vault is running behind a reverse proxy
exports.is_proxy = false;

// The disk quota per user in kilobytes
// 1mb = 1024kb = 1024 bytes / kb * 1024 kb / mb = 1048576 bytes / mb
exports.quota = 1024;

// The maximum patch size in kilobytes
exports.patchsize = 1;

// if testmode = true, there is no remote rippled connection made
// and no ecdsa signature check on create
exports.testmode = false

// if account is created before this date and funded
exports.nolimit_date = 'Thu May 1 2014';

// maximum length of a ripple username
exports.username_length = 20;

// time of day that emails are sent out to warn unfunded users they may lose their name
// 24 hour time, midnight = 0
// set for 8 AM
exports.schedule = {
    hour: 8,
    minute:0
}

// if you want to mark emails as being originated from a staging environment
exports.is_staging = false;

// authy phone verification
exports.phone = {
    url : 'http://sandbox-api.authy.com',
    key : '591b17780a53fb9872ec1f17f49e4fff'
}

//blockscore identity verification
exports.blockscore = {
    key : ''
}

//signed JWT iss
exports.issuer = "http://local.id.ripple.com";

// run campaign logic
// should be true from only one instance
exports.campaigns = false;


// Database settings
// 'mysql', 'memory', 'postgres'
exports.dbtype = 'postgres';
//exports.dbtype = 'mysql';
//exports.dbtype = 'memory';
exports.database = {
    mysql : {
        host     : '127.0.0.1',
        user     : 'blobvault',
        password : 'blobvault',
        database : 'blobvault',
        charset  : 'utf8'
    },
    postgres : {
        host     : '127.0.0.1',
        user     : 'blobvault',
        password : 'blobvault',
        database : 'blobvault',
        charset  : 'utf8'
    }
}

exports.email = {
    user: "",
    password: "" ,
    host:  "" ,
    from : "Company Name <email@example.com>"
}

exports.ripplelib = {
  trusted:        true,
  servers: [
    {
        host:    's1.ripple.com'
      , port:    443
      , secure:  false
    }
  ]
}

// PAKDF server setting
exports.defaultPakdfSetting = {
  "host": "local.auth1.ripple.com",
  "url": "http://local.auth1.ripple.com:3000/api/sign",
  "exponent": "010001",
  "alpha": "7283d19e784f48a96062271a4fa6e2c3addf14e6edf78a4bb61364856d580f13552008d7b9e3b60ebd9555e9f6c7778ec69f976757d206134e54d61ba9d588a7e37a77cf48060522478352d76db000366ef669a1b1ca93c5e3e05bc344afa1e8ccb15d3343da94180dccf590c2c32408c3f3f176c8885e95d988f1565ee9b80c12f72503ab49917792f907bbb9037487b0afed967fefc9ab090164597fcd391c43fab33029b38e66ff4af96cbf6d90a01b891f856ddd3d94e9c9b307fe01e1353a8c30edd5a94a0ebba5fe7161569000ad3b0d3568872d52b6fbdfce987a687e4b346ea702e8986b03b6b1b85536c813e46052a31ed64ec490d3ba38029544aa",
  "modulus": "c7f1bc1dfb1be82d244aef01228c1409c198894eca9e21430f1669b4aa3864c9f37f3d51b2b4ba1ab9e80f59d267fda1521e88b05117993175e004543c6e3611242f24432ce8efa3b81f0ff660b4f91c5d52f2511a6f38181a7bf9abeef72db056508bbb4eeb5f65f161dd2d5b439655d2ae7081fcc62fdcb281520911d96700c85cdaf12e7d1f15b55ade867240722425198d4ce39019550c4c8a921fc231d3e94297688c2d77cd68ee8fdeda38b7f9a274701fef23b4eaa6c1a9c15b2d77f37634930386fc20ec291be95aed9956801e1c76601b09c413ad915ff03bfdc0b6b233686ae59e8caf11750b509ab4e57ee09202239baee3d6e392d1640185e1cd"
};

exports.AUTHINFO_VERSION = 3;

// Reserved usernames
//
// Format:
//   exports.reserved = {
//     "google":"google.com"
//   }
//
// Becomes:
// "Sorry, the name "google" is reserved for the owner of "google.com" [Claim]"
//
// You can import the list externally:
//   exports.reserved = require('./names.json');
exports.reserved = {};

// If this blobvault is behind a reverse proxy, enter the url prefix that proxy
// is using.
//
// E.g. if the rediret is this:
// https://example.com/ripple-blobvault -> http://localhost:8080/
// Then this option should be set to this:
// exports.urlPrefix = '/ripple-blobvault';
exports.urlPrefix = '';

exports.newrelic = 'YOUR KEY';
