var assert = require('assert');
var elliptic = require('../');
var hash = require('hash.js');

describe('ECDSA', function() {
  function test(name) {
    it('should work with ' + name + ' curve', function() {
      this.timeout(5000);
      var curve = elliptic.curves[name];
      assert(curve);

      var ecdsa = new elliptic.ec(curve);
      var keys = ecdsa.genKeyPair({
        entropy: [
          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
          21, 22, 23, 24, 25
        ]
      });
      var msg = 'deadbeef';
      var keylen = 64;
      if (name === 'p384') {
        keylen = 96;
      } else if (name === 'p521') {
        keylen = 132
      }
      // Get keys out of pair
      assert(keys.getPublic().x && keys.getPublic().y);
      assert(keys.getPrivate().length > 0);
      assert.equal(keys.getPrivate('hex').length, keylen);
      assert(keys.getPublic('hex').length > 0);
      assert(keys.getPrivate('hex').length > 0);
      assert(keys.validate().result);

      // Sign and verify
      var signature = ecdsa.sign(msg, keys);
      assert(ecdsa.verify(msg, signature, keys), 'Normal verify');

      // Sign and verify on key
      var signature = keys.sign(msg);
      assert(keys.verify(msg, signature), 'On-key verify');

      // Load private key from hex
      var keys = ecdsa.keyFromPrivate(keys.getPrivate('hex'), 'hex');
      var signature = ecdsa.sign(msg, keys);
      assert(ecdsa.verify(msg, signature, keys), 'hex-private verify');

      // key.sign(msg, options)
      var sign = keys.sign('hello', { canonical: true });
      assert(sign.s.cmp(keys.ec.nh) <= 0);

      // Load public key from compact hex
      var keys = ecdsa.keyFromPublic(keys.getPublic(true, 'hex'), 'hex');

      // Load public key from hex
      var keys = ecdsa.keyFromPublic(keys.getPublic('hex'), 'hex');

      // DER encoding
      var dsign = signature.toDER('hex');
      assert(ecdsa.verify(msg, dsign, keys), 'hex-DER encoded verify');
      var dsign = signature.toDER();
      assert(ecdsa.verify(msg, dsign, keys), 'DER encoded verify');

      // Wrong public key
      var keys = ecdsa.genKeyPair();
      assert(!ecdsa.verify(msg, signature, keys), 'Wrong key verify');

      // Invalid private key
      var keys = ecdsa.keyFromPrivate(keys.getPrivate('hex') +
                                      keys.getPrivate('hex'));
      assert(!ecdsa.verify(msg, signature, keys), 'Wrong key verify');
    });
  }
  test('secp256k1');
  test('ed25519');
  test('p256');
  test('p384');
  test('p521');

  describe('RFC6979 vector', function() {
    function test(opt) {
      opt.cases.forEach(function(c) {
        var ecdsa = elliptic.ec({
          curve: opt.curve,
          hash: c.hash
        });
        var descr = 'should not fail on "' + opt.name + '" ' +
                    'and hash ' + c.hash.name + ' on "' + c.message + '"';
        it(descr, function() {
          var dgst = c.hash().update(c.message).digest();
          var sign = ecdsa.sign(dgst, opt.key);
          assert.equal(sign.r.toString(16), c.r);
          assert.equal(sign.s.toString(16), c.s);
          assert.ok(ecdsa.keyFromPublic(opt.pub).validate().result,
                    'Invalid public key');
          assert.ok(ecdsa.verify(dgst, sign, opt.pub),
                    'Invalid signature');
        });
      });
    }

    test({
      name: 'ECDSA, 192 Bits (Prime Field)',
      curve: elliptic.curves.p192,
      key: '6fab034934e4c0fc9ae67f5b5659a9d7d1fefd187ee09fd4',
      pub: {
        x: 'ac2c77f529f91689fea0ea5efec7f210d8eea0b9e047ed56',
        y: '3bc723e57670bd4887ebc732c523063d0a7c957bc97c1c43'
      },
      cases: [
        {
          message: 'sample',
          hash: hash.sha224,
          r: 'a1f00dad97aeec91c95585f36200c65f3c01812aa60378f5',
          s: 'e07ec1304c7c6c9debbe980b9692668f81d4de7922a0f97a'
        },
        {
          message: 'sample',
          hash: hash.sha256,
          r: '4b0b8ce98a92866a2820e20aa6b75b56382e0f9bfd5ecb55',
          s: 'ccdb006926ea9565cbadc840829d8c384e06de1f1e381b85'
        },
        {
          message: 'test',
          hash: hash.sha224,
          r: '6945a1c1d1b2206b8145548f633bb61cef04891baf26ed34',
          s: 'b7fb7fdfc339c0b9bd61a9f5a8eaf9be58fc5cba2cb15293'
        },
        {
          message: 'test',
          hash: hash.sha256,
          r: '3a718bd8b4926c3b52ee6bbe67ef79b18cb6eb62b1ad97ae',
          s: '5662e6848a4a19b1f1ae2f72acd4b8bbe50f1eac65d9124f'
        }
      ],
    });

    test({
      name: 'ECDSA, 224 Bits (Prime Field)',
      curve: elliptic.curves.p224,
      key: 'f220266e1105bfe3083e03ec7a3a654651f45e37167e88600bf257c1',
      pub: {
        x: '00cf08da5ad719e42707fa431292dea11244d64fc51610d94b130d6c',
        y: 'eeab6f3debe455e3dbf85416f7030cbd94f34f2d6f232c69f3c1385a'
      },
      cases: [
        {
          message: 'sample',
          hash: hash.sha224,
          r: '1cdfe6662dde1e4a1ec4cdedf6a1f5a2fb7fbd9145c12113e6abfd3e',
          s: 'a6694fd7718a21053f225d3f46197ca699d45006c06f871808f43ebc'
        },
        {
          message: 'sample',
          hash: hash.sha256,
          r: '61aa3da010e8e8406c656bc477a7a7189895e7e840cdfe8ff42307ba',
          s: 'bc814050dab5d23770879494f9e0a680dc1af7161991bde692b10101'
        },
        {
          message: 'test',
          hash: hash.sha224,
          r: 'c441ce8e261ded634e4cf84910e4c5d1d22c5cf3b732bb204dbef019',
          s: '902f42847a63bdc5f6046ada114953120f99442d76510150f372a3f4'
        },
        {
          message: 'test',
          hash: hash.sha256,
          r: 'ad04dde87b84747a243a631ea47a1ba6d1faa059149ad2440de6fba6',
          s: '178d49b1ae90e3d8b629be3db5683915f4e8c99fdf6e666cf37adcfd'
        }
      ],
    });

    test({
      name: 'ECDSA, 256 Bits (Prime Field)',
      curve: elliptic.curves.p256,
      key: 'c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721',
      pub: {
        x: '60fed4ba255a9d31c961eb74c6356d68c049b8923b61fa6ce669622e60f29fb6',
        y: '7903fe1008b8bc99a41ae9e95628bc64f2f1b20c2d7e9f5177a3c294d4462299'
      },
      cases: [
        {
          message: 'sample',
          hash: hash.sha224,
          r: '53b2fff5d1752b2c689df257c04c40a587fababb3f6fc2702f1343af7ca9aa3f',
          s: 'b9afb64fdc03dc1a131c7d2386d11e349f070aa432a4acc918bea988bf75c74c'
        },
        {
          message: 'sample',
          hash: hash.sha256,
          r: 'efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716',
          s: 'f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8'
        },
        {
          message: 'test',
          hash: hash.sha224,
          r: 'c37edb6f0ae79d47c3c27e962fa269bb4f441770357e114ee511f662ec34a692',
          s: 'c820053a05791e521fcaad6042d40aea1d6b1a540138558f47d0719800e18f2d'
        },
        {
          message: 'test',
          hash: hash.sha256,
          r: 'f1abb023518351cd71d881567b1ea663ed3efcf6c5132b354f28d3b0b7d38367',
          s: '19f4113742a2b14bd25926b49c649155f267e60d3814b4c0cc84250e46f0083'
        }
      ],
    });

    test({
      name: 'ECDSA, 384 Bits (Prime Field)',
      curve: elliptic.curves.p384,
      key: '6b9d3dad2e1b8c1c05b19875b6659f4de23c3b667bf297ba9aa4774078713' +
           '7d896d5724e4c70a825f872c9ea60d2edf5',
      pub: {
        x: 'ec3a4e415b4e19a4568618029f427fa5da9a8bc4ae92e02e06aae5286b30' +
           '0c64def8f0ea9055866064a254515480bc13',
        y: '8015d9b72d7d57244ea8ef9ac0c621896708a59367f9dfb9f54ca84b3f' +
           '1c9db1288b231c3ae0d4fe7344fd2533264720'
      },
      cases: [
        {
          message: 'sample',
          hash: hash.sha224,
          r: '42356e76b55a6d9b4631c865445dbe54e056d3b3431766d05092447' +
             '93c3f9366450f76ee3de43f5a125333a6be060122',
          s: '9da0c81787064021e78df658f2fbb0b042bf304665db721f077a429' +
             '8b095e4834c082c03d83028efbf93a3c23940ca8d'
        },
        {
          message: 'sample',
          hash: hash.sha384,
          r: '94edbb92a5ecb8aad4736e56c691916b3f88140666ce9fa73d6' +
             '4c4ea95ad133c81a648152e44acf96e36dd1e80fabe46',
          s: '99ef4aeb15f178cea1fe40db2603138f130e740a19624526203b' +
             '6351d0a3a94fa329c145786e679e7b82c71a38628ac8'
        },
        {
          message: 'test',
          hash: hash.sha384,
          r: '8203b63d3c853e8d77227fb377bcf7b7b772e97892a80f36a' +
             'b775d509d7a5feb0542a7f0812998da8f1dd3ca3cf023db',
          s: 'ddd0760448d42d8a43af45af836fce4de8be06b485e9b61b827c2f13' +
             '173923e06a739f040649a667bf3b828246baa5a5'
        }
      ],
    });

    test({
      name: 'ECDSA, 521 Bits (Prime Field)',
      curve: elliptic.curves.p521,
      key: '0fad06daa62ba3b25d2fb40133da757205de67f5bb0018fee8c86e1b68c7e75' +
           'caa896eb32f1f47c70855836a6d16fcc1466f6d8fbec67db89ec0c08b0e996b' +
           '83538',
      pub: {
        x: '1894550d0785932e00eaa23b694f213f8c3121f86dc97a04e5a7167db4e5bcd3' +
           '71123d46e45db6b5d5370a7f20fb633155d38ffa16d2bd761dcac474b9a2f502' +
           '3a4',
        y: '0493101c962cd4d2fddf782285e64584139c2f91b47f87ff82354d6630f746a2' +
           '8a0db25741b5b34a828008b22acc23f924faafbd4d33f81ea66956dfeaa2bfdfcf5'
      },
      cases: [
        {
          message: 'sample',
          hash: hash.sha384,
          r: '1ea842a0e17d2de4f92c15315c63ddf72685c18195c2bb95e572b9c5136ca4' +
             'b4b576ad712a52be9730627d16054ba40cc0b8d3ff035b12ae75168397f5' +
             'd50c67451',
          s: '1f21a3cee066e1961025fb048bd5fe2b7924d0cd797babe0a83b66f1e35ee' +
             'af5fde143fa85dc394a7dee766523393784484bdf3e00114a1c857cde1aa2' +
             '03db65d61'
        },
        {
          message: 'sample',
          hash: hash.sha512,
          r: 'c328fafcbd79dd77850370c46325d987cb525569fb63c5d3bc53950e6d4c5f1' +
             '74e25a1ee9017b5d450606add152b534931d7d4e8455cc91f9b15bf05ec36e37' +
             '7fa',
          s: '617cce7cf5064806c467f678d3b4080d6f1cc50af26ca209417308281b68af2' +
             '82623eaa63e5b5c0723d8b8c37ff0777b1a20f8ccb1dccc43997f1ee0e44da4a' +
             '67a'
        },
        {
          message: 'test',
          hash: hash.sha512,
          r: '13e99020abf5cee7525d16b69b229652ab6bdf2affcaef38773b4b7d087' +
             '25f10cdb93482fdcc54edcee91eca4166b2a7c6265ef0ce2bd7051b7cef945' +
             'babd47ee6d',
          s: '1fbd0013c674aa79cb39849527916ce301c66ea7ce8b80682786ad60f98' +
             'f7e78a19ca69eff5c57400e3b3a0ad66ce0978214d13baf4e9ac60752f7b15' +
             '5e2de4dce3'
        }
      ],
    });
  });

  it('should deterministically generate private key', function() {
    var curve = elliptic.curves.secp256k1;
    assert(curve);

    var ecdsa = new elliptic.ec(curve);
    var keys = ecdsa.genKeyPair({
      pers: 'my.pers.string',
      entropy: hash.sha256().update('hello world').digest()
    });
    assert.equal(
      keys.getPrivate('hex'),
      '6160edb2b218b7f1394b9ca8eb65a72831032a1f2f3dc2d99291c2f7950ed887');
  });

  it('should recover the public key from a signature', function(){
    var ec = new elliptic.ec('secp256k1');
    var key = ec.genKeyPair();
    var msg = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 ];
    var signature = key.sign(msg);
    var recid = ec.getKeyRecoveryParam(msg, signature, key.getPublic());
    var r =  ec.recoverPubKey(msg, signature, recid);
    assert(key.getPublic().eq(r), 'the keys should match');
  });
});
