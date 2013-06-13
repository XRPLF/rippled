

SETUP_LOG (ProofOfWork)

bool powResultInfo(POWResult powCode, std::string& strToken, std::string& strHuman)
{
	static struct {
		POWResult		powCode;
		const char*		cpToken;
		const char*		cpHuman;
	} powResultInfoA[] = {
		{	powREUSED,				"powREUSED",				"Proof-of-work has already been used."					},
		{	powBADNONCE,			"powBADNONCE",				"The solution does not meet the required difficulty."	},
		{	powEXPIRED,				"powEXPIRED",				"Token is expired."										},
		{	powCORRUPT,				"powCORRUPT",				"Invalid token."										},
		{	powTOOEASY,				"powTOOEASY",				"Difficulty has increased since token was issued."		},

		{	powOK,					"powOK",					"Valid proof-of-work."									},
	};

	int	iIndex	= NUMBER(powResultInfoA);

	while (iIndex-- && powResultInfoA[iIndex].powCode != powCode)
		;

	if (iIndex >= 0)
	{
		strToken	= powResultInfoA[iIndex].cpToken;
		strHuman	= powResultInfoA[iIndex].cpHuman;
	}

	return iIndex >= 0;
}

const uint256 ProofOfWork::sMinTarget("00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
const int ProofOfWork::sMaxIterations(1 << 23);
const int ProofOfWork::sMaxDifficulty(30);

ProofOfWork::ProofOfWork (const std::string& token,
                         int iterations,
                         uint256 const& challenge,
                         uint256 const& target)
    : mToken (token)
    , mChallenge (challenge)
    , mTarget (target)
    , mIterations (iterations)
{
}

ProofOfWork::ProofOfWork (const std::string& token)
{
	std::vector<std::string> fields;
	boost::split(fields, token, boost::algorithm::is_any_of("-"));
	if (fields.size() != 5)
		throw std::runtime_error("invalid token");

	mToken = token;
	mChallenge.SetHex(fields[0]);
	mTarget.SetHex(fields[1]);
	mIterations = lexical_cast_s<int>(fields[2]);
}

bool ProofOfWork::isValid() const
{
	if ((mIterations <= sMaxIterations) && (mTarget >= sMinTarget))
		return true;
	WriteLog (lsWARNING, ProofOfWork) << "Invalid PoW: " << mIterations << ", " << mTarget;
	return false;
}

uint64 ProofOfWork::getDifficulty(uint256 const& target, int iterations)
{ // calculate the approximate number of hashes required to solve this proof of work
	if ((iterations > sMaxIterations) || (target < sMinTarget))
	{
		WriteLog (lsINFO, ProofOfWork) << "Iterations:" << iterations;
		WriteLog (lsINFO, ProofOfWork) << "MaxIterat: " << sMaxIterations;
		WriteLog (lsINFO, ProofOfWork) << "Target:    " << target;
		WriteLog (lsINFO, ProofOfWork) << "MinTarget: " << sMinTarget;
		throw std::runtime_error("invalid proof of work target/iteration");
	}

	// more iterations means more hashes per iteration but also a larger final hash
	uint64 difficulty = iterations + (iterations / 8);

	// Multiply the number of hashes needed by 256 for each leading zero byte in the difficulty
	const unsigned char *ptr = target.begin();
	while (*ptr == 0)
	{
		difficulty *= 256;
		++ptr;
	}
	difficulty = (difficulty * 256) / (*ptr + 1);

	return difficulty;
}

static uint256 getSHA512Half(const std::vector<uint256>& vec)
{
	return Serializer::getSHA512Half(vec.front().begin(), vec.size() * (256 / 8));
}

uint256 ProofOfWork::solve(int maxIterations) const
{
	if (!isValid())
		throw std::runtime_error("invalid proof of work target/iteration");

	uint256 nonce;
	RandomNumbers::getInstance ().fill (&nonce);

	std::vector<uint256> buf2;
	buf2.resize(mIterations);

	std::vector<uint256> buf1;
	buf1.resize(3);
	buf1[0] = mChallenge;

	while (maxIterations > 0)
	{
		buf1[1] = nonce;
		buf1[2].zero();
		for (int i = (mIterations - 1); i >= 0; --i)
		{
			buf1[2] = getSHA512Half(buf1);
			buf2[i] = buf1[2];
		}

		if (getSHA512Half(buf2) <= mTarget)
			return nonce;

		++nonce;
		--maxIterations;
	}
	return uint256();
}

bool ProofOfWork::checkSolution(uint256 const& solution) const
{
	if (mIterations > sMaxIterations)
		return false;

	std::vector<uint256> buf1;
	buf1.push_back(mChallenge);
	buf1.push_back(solution);
	buf1.push_back(uint256());

	std::vector<uint256> buf2;
	buf2.resize(mIterations);
	for (int i = (mIterations - 1); i >= 0; --i)
	{
		buf1[2] = getSHA512Half(buf1);
		buf2[i] = buf1[2];
	}
	return getSHA512Half(buf2) <= mTarget;
}

bool ProofOfWork::validateToken(const std::string& strToken)
{
	static boost::regex	reToken("[[:xdigit:]]{64}-[[:xdigit:]]{64}-[[:digit:]]+-[[:digit:]]+-[[:xdigit:]]{64}");
	boost::smatch		smMatch;

	return boost::regex_match(strToken, smMatch, reToken);
}

// vim:ts=4
