
std::size_t hash_value(const uint256& u)
{
	std::size_t seed = theApp->getNonceST();

	return u.hash_combine(seed);
}

std::size_t hash_value(const uint160& u)
{
	std::size_t seed = theApp->getNonceST();

	return u.hash_combine(seed);
}

std::size_t hash_value(const CBase58Data& b58)
{
	std::size_t seed = theApp->getNonceST() + (b58.nVersion * 0x9e3779b9);
	boost::hash_combine(seed, b58.vchData);
	return seed;
}
