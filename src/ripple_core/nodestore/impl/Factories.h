//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_FACTORIES_H_INCLUDED
#define RIPPLE_NODESTORE_FACTORIES_H_INCLUDED

namespace NodeStore
{

// Holds the list of Backend factories
class Factories
{
public:
    Factories ()
    {
    }

    ~Factories ()
    {
    }

    void add (Factory* factory)
    {
        m_list.add (factory);
    }

    Factory* find (String name) const
    {
        for (int i = 0; i < m_list.size(); ++i)
            if (m_list [i]->getName ().compareIgnoreCase (name) == 0)
                return m_list [i];
        return nullptr;
    }

    static Factories& get ()
    {
        return *SharedSingleton <Factories>::get (
            SingletonLifetime::persistAfterCreation);
    }

private:
    OwnedArray <Factory> m_list;
};

}

#endif
