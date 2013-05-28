

#ifndef RIPPLE_DIFFIEHELLMANUTIL_H
#define RIPPLE_DIFFIEHELLMANUTIL_H

extern DH* DH_der_load (const std::string& strDer);
extern std::string DH_der_gen (int iKeyLength);

#endif

// vim:ts=4
