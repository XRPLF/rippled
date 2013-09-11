//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

class RippleSSLContextImp : public RippleSSLContext
{
private:
    boost::asio::ssl::context m_context;

public:
    RippleSSLContextImp ()
        : RippleSSLContext (m_context)
        , m_context (boost::asio::ssl::context::sslv23)
    {
    }

    ~RippleSSLContextImp ()
    {
    }

    static DH* tmp_dh_handler (SSL*, int, int key_length)
    {
        return DHparams_dup (getDH (key_length));
    }

    //--------------------------------------------------------------------------

    static std::string getRawDHParams (int keySize)
    {
        std::string params;

        // Original code provided the 512-bit keySize parameters
        // when 1024 bits were requested so we will do the same.
        if (keySize == 1024)
            keySize = 512;

        switch (keySize)
        {
        case 512:
            {
                // These are the DH parameters that OpenCoin has chosen for Ripple
                //
                uint8 const raw [] = {
                    0x30, 0x46, 0x02, 0x41, 0x00, 0x98, 0x15, 0xd2, 0xd0, 0x08, 0x32, 0xda,
                    0xaa, 0xac, 0xc4, 0x71, 0xa3, 0x1b, 0x11, 0xf0, 0x6c, 0x62, 0xb2, 0x35,
                    0x8a, 0x10, 0x92, 0xc6, 0x0a, 0xa3, 0x84, 0x7e, 0xaf, 0x17, 0x29, 0x0b,
                    0x70, 0xef, 0x07, 0x4f, 0xfc, 0x9d, 0x6d, 0x87, 0x99, 0x19, 0x09, 0x5b,
                    0x6e, 0xdb, 0x57, 0x72, 0x4a, 0x7e, 0xcd, 0xaf, 0xbd, 0x3a, 0x97, 0x55,
                    0x51, 0x77, 0x5a, 0x34, 0x7c, 0xe8, 0xc5, 0x71, 0x63, 0x02, 0x01, 0x02
                };

                params.resize (sizeof (raw));
                std::copy (raw, raw + sizeof (raw), params.begin ());
            }
            break;
        };

        return params;
    }

    //--------------------------------------------------------------------------

    // Does common initialization for all but the bare context type.
    void initCommon ()
    {
        m_context.set_options (
            boost::asio::ssl::context::default_workarounds |
            boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::single_dh_use);

        SSL_CTX_set_tmp_dh_callback (
            m_context.native_handle (),
            tmp_dh_handler);
    }

    //--------------------------------------------------------------------------

    void initAnonymous (String const& cipherList)
    {
        initCommon ();

        int const result = SSL_CTX_set_cipher_list (
            m_context.native_handle (),
            cipherList.toStdString ().c_str ());

        if (result != 1)
            FatalError ("invalid cipher list", __FILE__, __LINE__);
    }

    //--------------------------------------------------------------------------

    void initAuthenticated (
        std::string key_file, std::string cert_file, std::string chain_file)
    {
        initCommon ();

        SSL_CTX* const ssl = m_context.native_handle ();

        bool cert_set = false;

        if (! cert_file.empty ())
        {
            boost::system::error_code error;
            
            m_context.use_certificate_file (
                cert_file, boost::asio::ssl::context::pem, error);

            if (error)
            {
                FatalError ("Problem with SSL certificate file.",
                    __FILE__, __LINE__);
            }

            cert_set = true;
        }

        if (! chain_file.empty ())
        {
            // VFALCO Replace fopen() with RAII
            FILE* f = fopen (chain_file.c_str (), "r");

            if (!f)
            {
                FatalError ("Problem opening SSL chain file.",
                    __FILE__, __LINE__);
            }

            try
            {
                for (;;)
                {
                    X509* const x = PEM_read_X509 (f, NULL, NULL, NULL);

                    if (x == nullptr)
                        break;

                    if (! cert_set)
                    {
                        if (SSL_CTX_use_certificate (ssl, x) != 1)
                            FatalError ("Problem retrieving SSL certificate from chain file.",
                                __FILE__, __LINE__);

                        cert_set = true;
                    }
                    else if (SSL_CTX_add_extra_chain_cert (ssl, x) != 1)
                    {
                        X509_free (x);
                        FatalError ("Problem adding SSL chain certificate.",
                            __FILE__, __LINE__);
                    }
                }

                fclose (f);
            }
            catch (...)
            {
                fclose (f);
                FatalError ("Reading the SSL chain file generated an exception.",
                    __FILE__, __LINE__);
            }
        }

        if (! key_file.empty ())
        {
            boost::system::error_code error;

            m_context.use_private_key_file (key_file,
                boost::asio::ssl::context::pem, error);

            if (error)
            {
                FatalError ("Problem using the SSL private key file.",
                    __FILE__, __LINE__);
            }
        }

        if (SSL_CTX_check_private_key (ssl) != 1)
        {
            FatalError ("Invalid key in SSL private key file.",
                __FILE__, __LINE__);
        }
    }

    //--------------------------------------------------------------------------

    // A simple RAII container for a DH
    //
    struct ScopedDHPointer
    {
        // Construct from an existing DH
        //
        explicit ScopedDHPointer (DH* dh)
            : m_dh (dh)
        {
        }

        // Construct from raw DH params
        //
        explicit ScopedDHPointer (std::string const& params)
        {
            uint8 const* p (reinterpret_cast <uint8 const*>(&params [0]));
            m_dh = d2i_DHparams (nullptr, &p, params.size ());
            if (m_dh == nullptr)
                FatalError ("d2i_DHparams returned nullptr.",
                    __FILE__, __LINE__);
        }

        ~ScopedDHPointer ()
        {
            if (m_dh != nullptr)
                DH_free (m_dh);
        }

        operator DH* () const
        {
            return get ();
        }

        DH* get () const
        {
            return m_dh;
        }

    private:
        DH* m_dh;
    };

    //--------------------------------------------------------------------------

    static DH* getDH (int keyLength)
    {
        if (keyLength == 512 || keyLength == 1024)
        {
            static ScopedDHPointer dh512 (getRawDHParams (keyLength));

            return dh512.get ();
        }
        else
        {
            FatalError ("unsupported key length", __FILE__, __LINE__);
        }

        return nullptr;
    }
};

//------------------------------------------------------------------------------

RippleSSLContext::RippleSSLContext (ContextType& context)
    : SSLContext (context)
{
}

RippleSSLContext* RippleSSLContext::createBare ()
{
    ScopedPointer <RippleSSLContextImp> context (new RippleSSLContextImp ());

    return context.release ();
}

RippleSSLContext* RippleSSLContext::createWebSocket ()
{
    ScopedPointer <RippleSSLContextImp> context (new RippleSSLContextImp ());

    context->initCommon ();

    return context.release ();
}

RippleSSLContext* RippleSSLContext::createAnonymous (String const& cipherList)
{
    ScopedPointer <RippleSSLContextImp> context (new RippleSSLContextImp ());

    context->initAnonymous (cipherList);

    return context.release ();
}

RippleSSLContext* RippleSSLContext::createAuthenticated (
    std::string key_file, std::string cert_file, std::string chain_file)
{
    ScopedPointer <RippleSSLContextImp> context (new RippleSSLContextImp ());

    context->initAuthenticated (key_file, cert_file, chain_file);

    return context.release ();
}

std::string RippleSSLContext::getRawDHParams (int keySize)
{
    return RippleSSLContextImp::getRawDHParams (keySize);
}

