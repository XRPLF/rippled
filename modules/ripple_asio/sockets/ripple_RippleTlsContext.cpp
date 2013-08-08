//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

//------------------------------------------------------------------------------

class RippleTlsContextImp : public RippleTlsContext
{
public:
    RippleTlsContextImp ()
        : m_context (boost::asio::ssl::context::sslv23)
    {
        initBoostContext (m_context);
    }

    ~RippleTlsContextImp ()
    {
    }

    BoostContextType& getBoostContext () noexcept
    {
        return m_context;
    }

    //--------------------------------------------------------------------------

private:
    boost::asio::ssl::context m_context;
};

//------------------------------------------------------------------------------

void RippleTlsContext::initBoostContext (BoostContextType& context)
{
    struct Helpers
    {
        typedef boost::array <unsigned char, 72> RawDHParams;

        // A simple RAII container for a DH
        struct ScopedDHPointer
        {
            explicit ScopedDHPointer (DH* dh)
                : m_dh (dh)
            {
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
            unsigned char const* m_p;
            DH* m_dh;
        };

        //----------------------------------------------------------------------

        // These are the DH parameters that OpenCoin has chosen for Ripple
        //
        static RawDHParams const& getRaw512Params () noexcept
        {
            static RawDHParams params =
                { {
                0x30, 0x46, 0x02, 0x41, 0x00, 0x98, 0x15, 0xd2, 0xd0, 0x08, 0x32, 0xda,
                0xaa, 0xac, 0xc4, 0x71, 0xa3, 0x1b, 0x11, 0xf0, 0x6c, 0x62, 0xb2, 0x35,
                0x8a, 0x10, 0x92, 0xc6, 0x0a, 0xa3, 0x84, 0x7e, 0xaf, 0x17, 0x29, 0x0b,
                0x70, 0xef, 0x07, 0x4f, 0xfc, 0x9d, 0x6d, 0x87, 0x99, 0x19, 0x09, 0x5b,
                0x6e, 0xdb, 0x57, 0x72, 0x4a, 0x7e, 0xcd, 0xaf, 0xbd, 0x3a, 0x97, 0x55,
                0x51, 0x77, 0x5a, 0x34, 0x7c, 0xe8, 0xc5, 0x71, 0x63, 0x02, 0x01, 0x02
                } };

            return params;
        }

        static DH* createDH (RawDHParams const& rawParams)
        {
            RawDHParams::const_iterator iter = rawParams.begin ();
            return d2i_DHparams (nullptr, &iter, rawParams.size ());
        }

        //----------------------------------------------------------------------

        static DH* getDhParameters (int keyLength)
        {
            if (keyLength == 512 || keyLength == 1024)
            {
                static ScopedDHPointer dh512 (createDH (getRaw512Params ()));
                return dh512.get ();
            }
            else
            {
                FatalError ("unsupported key length", __FILE__, __LINE__);
            }

            return nullptr;
        }

        static DH* tmp_dh_handler (SSL*, int, int key_length)
        {
            return DHparams_dup (getDhParameters (key_length));
        }

        static char const* getCipherList ()
        {
            static char const* ciphers = "ALL:!LOW:!EXP:!MD5:@STRENGTH";

            return ciphers;
        }
    };

    //--------------------------------------------------------------------------

    context.set_options (
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::single_dh_use);

    context.set_verify_mode (boost::asio::ssl::verify_none);

    SSL_CTX_set_tmp_dh_callback (
        context.native_handle (),
        Helpers::tmp_dh_handler);

    int const result = SSL_CTX_set_cipher_list (
        context.native_handle (),
        Helpers::getCipherList ());

    if (result != 1)
        FatalError ("invalid cipher list", __FILE__, __LINE__);
}

//------------------------------------------------------------------------------

RippleTlsContext* RippleTlsContext::New ()
{
    return new RippleTlsContextImp ();
}
