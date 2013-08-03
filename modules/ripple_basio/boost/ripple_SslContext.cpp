//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace basio
{

SslContext* SslContext::New ()
{
    return new SslContext;
}

SslContext::~SslContext ()
{
}

SslContext::operator boost::asio::ssl::context& ()
{
    return *m_impl;
}

SslContext::SslContext ()
    : m_impl (new boost::asio::ssl::context (boost::asio::ssl::context::sslv23))
{
}

// VFALCO TODO Can we call this function from the ctor of PeerDoor as well?
//             Or can we move the common code to a new function?
//
void SslContext::initializeFromFile (
    boost::asio::ssl::context& context,
    std::string key_file,
    std::string cert_file,
    std::string chain_file)
{
    SSL_CTX* sslContext = context.native_handle ();

    context.set_options (boost::asio::ssl::context::default_workarounds |
                         boost::asio::ssl::context::no_sslv2 |
                         boost::asio::ssl::context::single_dh_use);

    bool cert_set = false;

    if (!cert_file.empty ())
    {
        boost::system::error_code error;
        context.use_certificate_file (cert_file, boost::asio::ssl::context::pem, error);

        if (error)
            throw std::runtime_error ("Unable to use certificate file");

        cert_set = true;
    }

    if (!chain_file.empty ())
    {
        // VFALCO Replace fopen() with RAII
        FILE* f = fopen (chain_file.c_str (), "r");

        if (!f)
            throw std::runtime_error ("Unable to open chain file");

        try
        {
            for (;;)
            {
                X509* x = PEM_read_X509 (f, NULL, NULL, NULL);

                if (x == NULL)
                    break;

                if (!cert_set)
                {
                    if (SSL_CTX_use_certificate (sslContext, x) != 1)
                        throw std::runtime_error ("Unable to get certificate from chain file");

                    cert_set = true;
                }
                else if (SSL_CTX_add_extra_chain_cert (sslContext, x) != 1)
                {
                    X509_free (x);
                    throw std::runtime_error ("Unable to add chain certificate");
                }
            }

            fclose (f);
        }
        catch (...)
        {
            fclose (f);
            throw;
        }
    }

    if (!key_file.empty ())
    {
        boost::system::error_code error;
        context.use_private_key_file (key_file, boost::asio::ssl::context::pem, error);

        if (error)
            throw std::runtime_error ("Unable to use private key file");
    }

    if (SSL_CTX_check_private_key (sslContext) != 1)
        throw std::runtime_error ("Private key not valid");
}

}
