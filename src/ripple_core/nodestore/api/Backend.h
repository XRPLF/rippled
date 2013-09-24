//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_BACKEND_H_INCLUDED
#define RIPPLE_NODESTORE_BACKEND_H_INCLUDED

namespace NodeStore
{

/** A backend used for the store.

    The NodeStore uses a swappable backend so that other database systems
    can be tried. Different databases may offer various features such
    as improved performance, fault tolerant or distributed storage, or
    all in-memory operation.

    A given instance of a backend is fixed to a particular key size.
*/
class Backend
{
public:
    /** Destroy the backend.

        All open files are closed and flushed. If there are batched writes
        or other tasks scheduled, they will be completed before this call
        returns.
    */
    virtual ~Backend () { }

    /** Get the human-readable name of this backend.

        This is used for diagnostic output.
    */
    virtual std::string getName() = 0;

    /** Fetch a single object.

        If the object is not found or an error is encountered, the
        result will indicate the condition.

        @note This will be called concurrently.

        @param key A pointer to the key data.
        @param pObject [out] The created object if successful.

        @return The result of the operation.
    */
    virtual Status fetch (void const* key, NodeObject::Ptr* pObject) = 0;

    /** Store a single object.

        Depending on the implementation this may happen immediately
        or deferred using a scheduled task.

        @note This will be called concurrently.

        @param object The object to store.
    */
    virtual void store (NodeObject::Ptr const& object) = 0;

    /** Store a group of objects.
            
        @note This function will not be called concurrently with
                itself or @ref store.
    */
    virtual void storeBatch (Batch const& batch) = 0;

    /** Visit every object in the database
            
        This is usually called during import.

        @note This routine will not be called concurrently with itself
                or other methods.

        @see import, VisitCallback
    */
    virtual void visitAll (VisitCallback& callback) = 0;

    /** Estimate the number of write operations pending. */
    virtual int getWriteLoad () = 0;
};

}

#endif
