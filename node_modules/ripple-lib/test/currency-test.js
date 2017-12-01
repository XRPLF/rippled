/*eslint-disable */

var assert   = require('assert');
var currency = require('ripple-lib').Currency;
var timeUtil = require('ripple-lib').utils.time;

describe('Currency', function() {
  describe('json_rewrite', function() {
    it('json_rewrite("USD") == "USD"', function() {
      assert.strictEqual('USD', currency.json_rewrite('USD'));
    });
    it('json_rewrite("NaN") == "XRP"', function() {
      assert.strictEqual('XRP', currency.json_rewrite(NaN));
    });
    it('json_rewrite("015841551A748AD2C1F76FF6ECB0CCCD00000000") == "XAU (-0.5%pa)"', function() {
      assert.strictEqual(currency.json_rewrite("015841551A748AD2C1F76FF6ECB0CCCD00000000"),
                         "XAU (-0.5%pa)");
    });
  });
  describe('from_json', function() {
    it('from_json().to_json() == "XRP"', function() {
      var r = currency.from_json();
      assert(!r.is_valid());
      assert.strictEqual('XRP', r.to_json());
    });
    it('from_json(NaN).to_json() == "XRP"', function() {
      var r = currency.from_json(NaN);
      assert(!r.is_valid());
      assert.strictEqual('XRP', r.to_json());
    });
    it('from_json().to_json("") == "XRP"', function() {
      var r = currency.from_json('');
      assert(r.is_valid());
      assert(r.is_native());
      assert.strictEqual('XRP', r.to_json());
    });
    it('from_json("XRP").to_json() == "XRP"', function() {
      var r = currency.from_json('XRP');
      assert(r.is_valid());
      assert(r.is_native());
      assert.strictEqual('XRP', r.to_json());
    });
    it('from_json("0000000000000000000000000000000000000000").to_json() == "XRP"', function() {
      var r = currency.from_json('0000000000000000000000000000000000000000');
      assert(r.is_valid());
      assert(r.is_native());
      assert.strictEqual('XRP', r.to_json());
    });
    it('from_json("111").to_human()', function() {
      var r = currency.from_json("111");
      assert(r.is_valid());
      assert.strictEqual('111', r.to_json());
    });
    it('from_json("1D2").to_human()', function() {
      var r = currency.from_json("1D2");
      assert(r.is_valid());
      assert.strictEqual('1D2', r.to_json());
    });
    it('from_json("1").to_human()', function() {
      var r = currency.from_json('1');
      assert(r.is_valid());
      assert.strictEqual(1, r.to_json());
    });
    it('from_json("#$%").to_human()', function() {
      var r = currency.from_json('#$%');
      assert(r.is_valid());
      assert.strictEqual('0000000000000000000000002324250000000000', r.to_json());
    });
    it('from_json("XAU").to_json() hex', function() {
      var r = currency.from_json("XAU");
      assert.strictEqual('0000000000000000000000005841550000000000', r.to_json({force_hex: true}));
    });
    it('from_json("XAU (0.5%pa").to_json() hex', function() {
      var r = currency.from_json("XAU (0.5%pa)");
      assert.strictEqual('015841550000000041F78E0A28CBF19200000000', r.to_json({force_hex: true}));
    });
    it('json_rewrite("015841550000000041F78E0A28CBF19200000000").to_json() hex', function() {
      var r = currency.json_rewrite('015841550000000041F78E0A28CBF19200000000');
      assert.strictEqual('XAU (0.5%pa)', r);
    });
    it('json_rewrite("015841550000000041F78E0A28CBF19200000000") hex', function() {
      var r = currency.json_rewrite('015841550000000041F78E0A28CBF19200000000', {force_hex: true});
      assert.strictEqual('015841550000000041F78E0A28CBF19200000000', r);
    });
  });

  describe('from_human', function() {
    it('From human "USD - Gold (-25%pa)"', function() {
      var cur = currency.from_human('USD - Gold (-25%pa)');
      assert.strictEqual(cur.to_json(), 'USD (-25%pa)');
      assert.strictEqual(cur.to_hex(), '0155534400000000C19A22BC51297F0B00000000');
      assert.strictEqual(cur.to_json(), cur.to_human());
    });
    it('From human "EUR (-0.5%pa)', function() {
      var cur = currency.from_human('EUR (-0.5%pa)');
      assert.strictEqual(cur.to_json(), 'EUR (-0.5%pa)');
    });
    it('From human "EUR (0.5361%pa)", test decimals', function() {
      var cur = currency.from_human('EUR (0.5361%pa)');
      assert.strictEqual(cur.to_json(), 'EUR (0.54%pa)');
      assert.strictEqual(cur.to_json({decimals:4}), 'EUR (0.5361%pa)');
      assert.strictEqual(cur.get_interest_percentage_at(undefined, 4), 0.5361);
    });
    it('From human "EUR - Euro (0.5361%pa)", test decimals and full_name', function() {
      var cur = currency.from_human('EUR (0.5361%pa)');
      assert.strictEqual(cur.to_json(), 'EUR (0.54%pa)');
      assert.strictEqual(cur.to_json({decimals:4, full_name:'Euro'}), 'EUR - Euro (0.5361%pa)');
      assert.strictEqual(cur.to_json({decimals:void(0), full_name:'Euro'}), 'EUR - Euro (0.54%pa)');
      assert.strictEqual(cur.to_json({decimals:undefined, full_name:'Euro'}), 'EUR - Euro (0.54%pa)');
      assert.strictEqual(cur.to_json({decimals:'henk', full_name:'Euro'}), 'EUR - Euro (0.54%pa)');
      assert.strictEqual(cur.get_interest_percentage_at(undefined, 4), 0.5361);
    });
    it('From human "TYX - 30-Year Treasuries (1.5%pa)"', function() {
      var cur = currency.from_human('TYX - 30-Year Treasuries (1.5%pa)');
      assert.strictEqual(cur.to_json(), 'TYX (1.5%pa)');
    });
    it('From human "TYX - 30-Year Treasuries"', function() {
      var cur = currency.from_human('TYX - 30-Year Treasuries');
      assert.strictEqual(cur.to_json(), 'TYX');
    });
    it('From human "INR - Indian Rupees (-0.5%)"', function() {
      var cur = currency.from_human('INR - Indian Rupees (-0.5%pa)');
      assert.strictEqual(cur.to_json(), 'INR (-0.5%pa)');
    });
    it('From human "INR - 30 Indian Rupees"', function() {
      var cur = currency.from_human('INR - 30 Indian Rupees');
      assert.strictEqual(cur.to_json(), 'INR');
    });
    it('From human "XRP"', function() {
      var cur = currency.from_human('XRP');
      assert.strictEqual(cur.to_json(), 'XRP');
      assert(cur.is_native(), true);
    });
    it('From human "XRP - Ripples"', function() {
      var cur = currency.from_human('XRP - Ripples');
      assert.strictEqual(cur.to_json(), 'XRP');
      assert(cur.is_native(), true);
    });

  });

  describe('to_human', function() {
    it('"USD".to_human() == "USD"', function() {
      assert.strictEqual('USD', currency.from_json('USD').to_human());
    });
    it('"NaN".to_human() == "XRP"', function() {
      assert.strictEqual('XRP', currency.from_json(NaN).to_human());
    });
    it('"015841551A748AD2C1F76FF6ECB0CCCD00000000") == "015841551A748AD2C1F76FF6ECB0CCCD00000000"', function() {
      assert.strictEqual(currency.from_json("015841551A748AD2C1F76FF6ECB0CCCD00000000").to_human(), 'XAU (-0.5%pa)');
    });
    it('"015841551A748AD2C1F76FF6ECB0CCCD00000000") == "015841551A748AD2C1F76FF6ECB0CCCD00000000"', function() {
      assert.strictEqual(currency.from_json("015841551A748AD2C1F76FF6ECB0CCCD00000000").to_human({full_name:'Gold'}), 'XAU - Gold (-0.5%pa)');
    });
    it('to_human interest XAU with full name, do not show interest', function() {
      assert.strictEqual(currency.from_json("015841551A748AD2C1F76FF6ECB0CCCD00000000").to_human({full_name:'Gold', show_interest:false}), 'XAU - Gold');
    });
    it('to_human interest XAU with full name, show interest', function() {
      assert.strictEqual(currency.from_json("015841551A748AD2C1F76FF6ECB0CCCD00000000").to_human({full_name:'Gold', show_interest:true}), 'XAU - Gold (-0.5%pa)');
    });
    it('to_human interest XAU, do show interest', function() {
      assert.strictEqual(currency.from_json("015841551A748AD2C1F76FF6ECB0CCCD00000000").to_human({show_interest:true}), 'XAU (-0.5%pa)');
    });
    it('to_human interest XAU, do not show interest', function() {
      assert.strictEqual(currency.from_json("015841551A748AD2C1F76FF6ECB0CCCD00000000").to_human({show_interest:false}), 'XAU');
    });
    it('to_human with full_name "USD - US Dollar show interest"', function() {
      assert.strictEqual(currency.from_json('USD').to_human({full_name:'US Dollar', show_interest:true}), 'USD - US Dollar (0%pa)');
    });
    it('to_human with full_name "USD - US Dollar do not show interest"', function() {
      assert.strictEqual(currency.from_json('USD').to_human({full_name:'US Dollar', show_interest:false}), 'USD - US Dollar');
    });
    it('to_human with full_name "USD - US Dollar"', function() {
      assert.strictEqual('USD - US Dollar', currency.from_json('USD').to_human({full_name:'US Dollar'}));
    });
    it('to_human with full_name "XRP - Ripples"', function() {
      assert.strictEqual('XRP - Ripples', currency.from_json('XRP').to_human({full_name:'Ripples'}));
    });
    it('to_human human "TIM" without full_name', function() {
      var cur = currency.from_json("TIM");
      assert.strictEqual(cur.to_human(), "TIM");
    });
    it('to_human "TIM" with null full_name', function() {
      var cur = currency.from_json("TIM");
      assert.strictEqual(cur.to_human({full_name: null}), "TIM");
    });

  });

  describe('from_hex', function() {
    it('"015841551A748AD2C1F76FF6ECB0CCCD00000000" === "XAU (-0.5%pa)"', function() {
      var cur = currency.from_hex('015841551A748AD2C1F76FF6ECB0CCCD00000000');
      assert.strictEqual(cur.to_json(), 'XAU (-0.5%pa)');
      assert.strictEqual(cur.to_hex(), '015841551A748AD2C1F76FF6ECB0CCCD00000000');
      assert.strictEqual(cur.to_json(), cur.to_human());
    });
  });
  describe('parse_json', function() {
    it('should parse a currency object', function() {
      assert.strictEqual('USD', new currency().parse_json(currency.from_json('USD')).to_json());
      assert.strictEqual('USD (0.5%pa)', new currency().parse_json(currency.from_json('USD (0.5%pa)')).to_json());
    });
    it('should clone for parse_json on itself', function() {
      var cur = currency.from_json('USD');
      var cur2 = currency.from_json(cur);
      assert.strictEqual(cur.to_json(), cur2.to_json());

      cur = currency.from_hex('015841551A748AD2C1F76FF6ECB0CCCD00000000');
      cur2 = currency.from_json(cur);
      assert.strictEqual(cur.to_json(), cur2.to_json());
    });
    it('should parse json 0', function() {
      var cur = currency.from_json(0);
      assert.strictEqual(cur.to_json(), 'XRP');
      assert.strictEqual(cur.get_iso(), 'XRP');
    });
    it('should parse json 0', function() {
      var cur = currency.from_json('0');
      assert.strictEqual(cur.to_json(), 'XRP');
      assert.strictEqual(cur.get_iso(), 'XRP');
    });
  });

  describe('is_valid', function() {
    it('Currency.is_valid("XRP")', function() {
      assert(currency.is_valid('XRP'));
    });
    it('!Currency.is_valid(NaN)', function() {
      assert(!currency.is_valid(NaN));
    });
    it('from_json("XRP").is_valid()', function() {
      assert(currency.from_json('XRP').is_valid());
    });
    it('!from_json(NaN).is_valid()', function() {
      assert(!currency.from_json(NaN).is_valid());
    });
  });
  describe('clone', function() {
    it('should clone currency object', function() {
      var c = currency.from_json('XRP');
      assert.strictEqual('XRP', c.clone().to_json());
    });
  });
  describe('to_human', function() {
    it('should generate human string', function() {
      assert.strictEqual('XRP', currency.from_json('XRP').to_human());
    });
  });
  describe('has_interest', function() {
    it('should be true for type 1 currency codes', function() {
      assert(currency.from_hex('015841551A748AD2C1F76FF6ECB0CCCD00000000').has_interest());
      assert(currency.from_json('015841551A748AD2C1F76FF6ECB0CCCD00000000').has_interest());
    });
    it('should be false for type 0 currency codes', function() {
      assert(!currency.from_hex('0000000000000000000000005553440000000000').has_interest());
      assert(!currency.from_json('USD').has_interest());
    });
  });
  function precision(num, precision) {
    return +(Math.round(num + "e+"+precision)  + "e-"+precision);
  }
  describe('get_interest_at', function() {
    it('should return demurred value for demurrage currency', function() {
      var cur = currency.from_json('015841551A748AD2C1F76FF6ECB0CCCD00000000');

      // At start, no demurrage should occur
      assert.equal(1, cur.get_interest_at(443845330));
      assert.equal(1, precision(cur.get_interest_at(new Date(timeUtil.fromRipple(443845330))), 14));

      // After one year, 0.5% should have occurred
      assert.equal(0.995, precision(cur.get_interest_at(443845330 + 31536000), 14));
      assert.equal(0.995, precision(cur.get_interest_at(new Date(timeUtil.fromRipple(443845330 + 31536000))), 14));

      // After one demurrage period, 1/e should have occurred
      var epsilon = 1e-14;
      assert(Math.abs(
        1/Math.E - cur.get_interest_at(443845330 + 6291418827.05)) < epsilon);

      // One year before start, it should be (roughly) 0.5% higher.
      assert.equal(1.005, precision(cur.get_interest_at(443845330 - 31536000), 4));

      // One demurrage period before start, rate should be e
      assert.equal(Math.E, cur.get_interest_at(443845330 - 6291418827.05));
    });
    it('should return 0 for currency without interest', function() {
      var cur = currency.from_json('USD - US Dollar');
      assert.equal(0, cur.get_interest_at(443845330));
      assert.equal(0, cur.get_interest_at(443845330  + 31536000));
    });
  });
  describe('get_iso', function() {
    it('should get "XRP" iso_code', function() {
      assert.strictEqual('XRP', currency.from_json('XRP').get_iso());
    });
    it('should get iso_code', function() {
      assert.strictEqual('USD', currency.from_json('USD - US Dollar').get_iso());
    });
    it('should get iso_code', function() {
      assert.strictEqual('USD', currency.from_json('USD (0.5%pa)').get_iso());
    });
  });
});
