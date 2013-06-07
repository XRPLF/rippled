
#ifndef BEAST_SCOPEDPOINTER_BEASTHEADER
#define BEAST_SCOPEDPOINTER_BEASTHEADER

template <class ObjectType>
class ScopedPointer
{
public:
    inline ScopedPointer()    : object (0)
    {
    }

    inline ScopedPointer (ObjectType* const objectToTakePossessionOf) 
        : object (objectToTakePossessionOf)
    {
    }

    ScopedPointer (ScopedPointer& objectToTransferFrom) 
        : object (objectToTransferFrom.object)
    {
        objectToTransferFrom.object = 0;
    }

    inline ~ScopedPointer()                                                         { delete object; }

    ScopedPointer& operator= (ScopedPointer& objectToTransferFrom)
    {
        if (this != objectToTransferFrom.getAddress())
        {
            ObjectType* const oldObject = object;
            object = objectToTransferFrom.object;
            objectToTransferFrom.object = 0;
            delete oldObject;
        }

        return *this;
    }

    ScopedPointer& operator= (ObjectType* const newObjectToTakePossessionOf)
    {
        if (object != newObjectToTakePossessionOf)
        {
            ObjectType* const oldObject = object;
            object = newObjectToTakePossessionOf;
            delete oldObject;
        }

        return *this;
    }

    inline operator ObjectType*() const                                     { return object; }
    inline ObjectType* get() const                                          { return object; }
    inline ObjectType& operator*() const                                    { return *object; }
    inline ObjectType* operator->() const                                   { return object; }

    ObjectType* release()                                                   { ObjectType* const o = object; object = 0; return o; }

    void swapWith (ScopedPointer <ObjectType>& other) 
    {
        std::swap (object, other.object);
    }

private:
    ObjectType* object;

    const ScopedPointer* getAddress() const                                 { return this; }

  #ifndef _MSC_VER
    ScopedPointer (const ScopedPointer&);
    ScopedPointer& operator= (const ScopedPointer&);
  #endif
};

template <class ObjectType>
bool operator== (const ScopedPointer<ObjectType>& pointer1, ObjectType* const pointer2) 
{
    return static_cast <ObjectType*> (pointer1) == pointer2;
}

template <class ObjectType>
bool operator!= (const ScopedPointer<ObjectType>& pointer1, ObjectType* const pointer2) 
{
    return static_cast <ObjectType*> (pointer1) != pointer2;
}

#endif
