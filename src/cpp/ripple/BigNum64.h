
// Support 64-bit word operations on 32-bit platforms

static int BN_add_word64(BIGNUM *a, uint64 w)
{
	CBigNum bn(w);
	return BN_add(a, &bn, a);
}

static int BN_sub_word64(BIGNUM *a, uint64 w)
{
	CBigNum bn(w);
	return BN_sub(a, &bn, a);
}

static int BN_mul_word64(BIGNUM *a, uint64 w)
{
	CBigNum bn(w);
	CAutoBN_CTX ctx;
	return BN_mul(a, &bn, a, ctx);
}

static uint64 BN_div_word64(BIGNUM *a, uint64 w)
{
	CBigNum bn(w);
	CAutoBN_CTX ctx;
	return (BN_div(a, NULL, a, &bn, ctx) == 1) ? 0 : ((uint64)-1);
}
