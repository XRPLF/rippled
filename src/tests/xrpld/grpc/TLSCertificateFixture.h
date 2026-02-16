#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace xrpl {
namespace test {

namespace TestCertificates {

constexpr std::string_view CA_CERT = R"(-----BEGIN CERTIFICATE-----
MIIFjzCCA3egAwIBAgIUB0zsfrjUOpBIofCIJ4zYp4yOk+wwDQYJKoZIhvcNAQEL
BQAwVzELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx
GDAWBgNVBAoMD1JpcHBsZWQgVGVzdCBDQTEQMA4GA1UEAwwHVGVzdCBDQTAeFw0y
NjAyMTYxNDA4MzFaFw0yNzAyMTYxNDA4MzFaMFcxCzAJBgNVBAYTAlVTMQ0wCwYD
VQQIDARUZXN0MQ0wCwYDVQQHDARUZXN0MRgwFgYDVQQKDA9SaXBwbGVkIFRlc3Qg
Q0ExEDAOBgNVBAMMB1Rlc3QgQ0EwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIK
AoICAQDViwgofOnD2Ua5kPyGvYkXmcXI72gjpT2HdGi4qMsyov5UGI3HhuwM9rw3
FWOrzB93S6plgeq0JjkjvYutD1Hl7QNhhybqVOZ+S5rnqStEyM33WfB4yX50N36B
8+DAFCMcJB1cdPKTpupQHTwI6dvHOzpX2fdKyJ0O/oonJApT3XdOm8fC8Z5m+jsD
RV39Z91k4noMxzmpF34N9BJtqvtCDcmbWrFEyCl+Dew33nzKSuBwNbrJ1IAsRbfs
38zk5V+T2ybrgQ7c+RX5iGxPDltPQb2+dPLfipXMoL7fj6nJuNV9uIG9ePb9agVU
E9Jdu9sXpKDUvm1RbL7fN8yUyQ/EdNCsO8GlpYZTInE2q9sHQGkHl/ktIdSHTEQz
Q3lWPPdWyRji3MEZeIMW+HttObEo1SRB3pG47E8T/haudCz0Cvpq3LW7TcpAlk/v
5pLUIuTkuwIcsUKZeIJMzbgXjTUe24beijGlQUKr/OkJC2DqQcnGrWPSBa9eBxkc
z5J1//XtcYmlWFDyfviHxo8RzW20RTj8XbfCWhZOCRDG3j7B8nHPBw4Bu2aX6+7m
003mcqZRKxEKr3nP4qBcSJH3ynKTAovDfmIW5tywa+ORQiKCmRS3hUaVh5YBJfk0
y9rv4CSxt27H8OaPehMMU2Pd1vPgo6XX8k2hYd9BJyq2Sj7fuwIDAQABo1MwUTAd
BgNVHQ4EFgQUk0E67n/wdKVL6mF0pMJ6N23kutswHwYDVR0jBBgwFoAUk0E67n/w
dKVL6mF0pMJ6N23kutswDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOC
AgEAVzMIdDa+0BevWeGESNnl3Uuo+L4ZGjqwLDvvyJyEQy64bs40QtJYu+vtFipC
yjDtH+WbsGiDZA8FDRZNkJ36cx1I2M3xOt6Ovx53+PDnRv0V+o8F5X/CMK9/64iI
o5g/HsdjYmrkGgyBx/L7FWQ7yLDECsTrRNdXskX4Q514EpNFFywW5l3VaKCTA6z1
vUgCi6Wm1kerw9syU/IjfK2/razO+Sxy5l5i/2jrDqemCFp2fXABb2DuGKhwuoYK
DN5pLJbhIFzihVEc2ChZO8loCKSYOKB8z04bEkJyBFebHxs4hXMo60Sr3H0WUK1C
gYKApmtN6riCKxccUo5WeZt/9rRVPQ16ogWp8yZRaEPO7bXPbuXMua9D+Hx+JaO7
3IdxJzQmdcS4ZUF02LGdUI+1WHFTfNddXLniU0Wa2pxPX9Gb0UPnpLnOPihrD24t
qzOSRZn9CR7VA+8peaW7P1dxwU5qtRjAG/xk8GPXL6vQXiH2j45iqz6rbKunMpMm
2px/RcoCno2wV+m5Ff2imNVPiU/R3gxqIO508Gf0csuJjC0OhD9UoIW52plCs7zi
qukxizXZ/SLNjipwPJT+JzcrB2JaCl23WsgSY2F1TgD0gajrPZzG1A57ujT2n3uC
3IxrK/Bbvv7GK5vJP7GcBmZAWw9IgaTIyQiwnm9YSuwMGBw=
-----END CERTIFICATE-----
)";

constexpr std::string_view SERVER_CERT = R"(-----BEGIN CERTIFICATE-----
MIIFlDCCA3ygAwIBAgIUXlAnHAM2/N1x11BsnxVkQPqt8iMwDQYJKoZIhvcNAQEL
BQAwVzELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx
GDAWBgNVBAoMD1JpcHBsZWQgVGVzdCBDQTEQMA4GA1UEAwwHVGVzdCBDQTAeFw0y
NjAyMTYxNDA4MzFaFw0yNzAyMTYxNDA4MzFaMFExCzAJBgNVBAYTAlVTMQ0wCwYD
VQQIDARUZXN0MQ0wCwYDVQQHDARUZXN0MRAwDgYDVQQKDAdSaXBwbGVkMRIwEAYD
VQQDDAlsb2NhbGhvc3QwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQCe
0C+Xo3VpTRPjP+cvCsEU0EbiL9Pghknwj6HwVZeBQidEp5ooqY4m1K6/HnAM3vWg
H8a5nxesqXnrHo1Od7xwaVOACp/doSeZCnBbnopNjAnAC/SmlGO1gaF4koFRqOrj
d7TZXN+dp+4WuhNcfHrkswRPeQOukRf3aEwAdPPAq90J698R3yck47Ylphk8v2nB
tBObnFWAYi+HJooe57Mmqupj2OmNTazer0nLTQEwVgvdnGweIiR6YVcnGOBf6Lu2
YK4Dwl1RFJurxO7TL43ziq2WvmpEHvHJ62YT4ze3Ik5EVOrYUjjADhKAoc9etBMP
b+sQqnBRzPJBtNE8c65gIlrwG6B+oJ7A4/J1aYd9n85k/z2ppKNAZTv8HBxeU2bu
vGNxD+qHr6j4r2nlbsJH/0mcb5IJykAFhwowM2/Q4h9yTrLDVNEUs8PcshPW7O9f
hKdl8blybSZ395aXRVqk9umGdkwKGjuSZJSQxIMJhHWpf5AobL8DDZ2vuUwmA69O
tD7i7XUBqAkMB+VBfSp1XMqRIM1PCfhXrTb9yTiqNRZYRAMIzxvkB6ZIxRJjJDLf
gPqpIS6lKZedl4K/FFwBRh/1Qyd8gK1ozk2VIZERkiBf49xy9vz79Ky3Sr4VMpwJ
3S46MJXb/60e+4O+piLcXchGYZcFHXafIXkq/4crRQIDAQABo14wXDAaBgNVHREE
EzARgglsb2NhbGhvc3SHBH8AAAEwHQYDVR0OBBYEFO2AI+yJSHRRGKINyX7uzAJV
Mc3nMB8GA1UdIwQYMBaAFJNBOu5/8HSlS+phdKTCejdt5LrbMA0GCSqGSIb3DQEB
CwUAA4ICAQAtO0GWUYBVqEaeAICdX0h9EpDHuKJXPLMPt/aOqQY0sI5ryks+/QLe
0ksOde2AmDW4maWi9toILT7WZg7qRGQGZc0Pk0LgU/JANVJaQPHPJvQcgcm2A8CT
Td36+K4MLrDd8mViCczaUebmO182RKIcSV47XfQbEpQLVwZKC7AufNRaRhUko5X3
lNb7KgZTNcQAZ5gvfJZ3b+3GzbuRz7WOaPyE94yy3D+6ML7XEd7rNoYKFpsrWjh8
0XWJQlX41X6orsi6g9pzprtOgl2PoJvGBSAd/Pp4jxnRVNflxxAiFaoWFxksOsuu
CiKQkB696AhRcPZyFk60ubeEo3epv2u1kjHSP3HTxe1iIO97qsL7zxfEB5lx6ubB
SylV2s23ysCCSqjbHkGgI/vVkHOPWHsCMdLjPuBUB5K9rfT0pBrCVss6mTr8D/7m
uy2rRWMXvPVKlxY7FhRTCpWMwBbe7xojGtniqXWsQml2nv/wMiUbtfwvNhFbqFjK
zwI9GnULtjmtpXkGzHGOaiG8ZAsc7cgsahKdFq+EfgmeHV4Gy01mb+UIT3npdpeu
YSUo7dfqdzPSZcKJgtsnhta24qyFlw93YlN1LJj/Mt0YKRNKcbiRYTSmh0e0bjM+
Uv20DqFMwRtULPOka+qw22vzEUvZcX2SdEcGpb8Ej1oHrgo4wE0gCQ==
-----END CERTIFICATE-----
)";

constexpr std::string_view SERVER_KEY = R"(-----BEGIN PRIVATE KEY-----
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
5dPKrvLYFOCfSjtu7qDDs9AH7FOo0VI=
-----END PRIVATE KEY-----
)";

}  // namespace TestCertificates

/**
 * RAII helper for managing temporary TLS certificates in tests.
 *
 * Creates a temporary directory and writes test certificates to it.
 * Automatically cleans up the directory when destroyed.
 */
class TemporaryTLSCertificates
{
public:
    static constexpr std::string_view CA_CERT_FILENAME = "ca.pem";
    static constexpr std::string_view SERVER_CERT_FILENAME = "server_cert.pem";
    static constexpr std::string_view SERVER_KEY_FILENAME = "server_key.pem";
    static constexpr std::string_view CERTS_DIR_PREFIX = "grpc_tls_test_";

    TemporaryTLSCertificates()
    {
        auto tmpDir = std::filesystem::temp_directory_path();
        tempDir_ = tmpDir / (std::string(CERTS_DIR_PREFIX) + std::to_string(std::time(nullptr)));
        std::filesystem::create_directories(tempDir_);

        writeFile(tempDir_ / CA_CERT_FILENAME, TestCertificates::CA_CERT);
        writeFile(tempDir_ / SERVER_CERT_FILENAME, TestCertificates::SERVER_CERT);
        writeFile(tempDir_ / SERVER_KEY_FILENAME, TestCertificates::SERVER_KEY);
    }

    virtual ~TemporaryTLSCertificates()
    {
        std::error_code ec;
        std::filesystem::remove_all(tempDir_, ec);
    }

    TemporaryTLSCertificates(TemporaryTLSCertificates const&) = delete;
    TemporaryTLSCertificates&
    operator=(TemporaryTLSCertificates const&) = delete;
    TemporaryTLSCertificates(TemporaryTLSCertificates&&) = delete;
    TemporaryTLSCertificates&
    operator=(TemporaryTLSCertificates&&) = delete;

    std::filesystem::path
    getCACertPath() const
    {
        return tempDir_ / CA_CERT_FILENAME;
    }

    std::filesystem::path
    getServerCertPath() const
    {
        return tempDir_ / SERVER_CERT_FILENAME;
    }

    std::filesystem::path
    getServerKeyPath() const
    {
        return tempDir_ / SERVER_KEY_FILENAME;
    }

    std::filesystem::path
    getTempDir() const
    {
        return tempDir_;
    }

private:
    void
    writeFile(std::filesystem::path const& path, std::string_view content)
    {
        std::ofstream file(path);
        if (!file)
            throw std::runtime_error("Failed to create file: " + path.string());
        file << content;
        if (!file)
            throw std::runtime_error("Failed to write file: " + path.string());
    }

    std::filesystem::path tempDir_;
};

}  // namespace test
}  // namespace xrpl
