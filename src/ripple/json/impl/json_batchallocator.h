//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_JSON_JSON_BATCHALLOCATOR_H_INCLUDED
#define RIPPLE_JSON_JSON_BATCHALLOCATOR_H_INCLUDED

namespace Json
{

/* Fast memory allocator.
 *
 * This memory allocator allocates memory for a batch of object (specified by
 * the page size, the number of object in each page).
 *
 * It does not allow the destruction of a single object. All the allocated objects
 * can be destroyed at once. The memory can be either released or reused for future
 * allocation.
 *
 * The in-place new operator must be used to construct the object using the pointer
 * returned by allocate.
 */
template < typename AllocatedType
, const unsigned int objectPerAllocation >
class BatchAllocator
{
public:
    using Type = AllocatedType;

    BatchAllocator ( unsigned int objectsPerPage = 255 )
        : freeHead_ ( 0 )
        , objectsPerPage_ ( objectsPerPage )
    {
        //      printf( "Size: %d => %s\n", sizeof(AllocatedType), typeid(AllocatedType).name() );
        assert ( sizeof (AllocatedType) * objectPerAllocation >= sizeof (AllocatedType*) ); // We must be able to store a slist in the object free space.
        assert ( objectsPerPage >= 16 );
        batches_ = allocateBatch ( 0 );  // allocated a dummy page
        currentBatch_ = batches_;
    }

    ~BatchAllocator ()
    {
        for ( BatchInfo* batch = batches_; batch;  )
        {
            BatchInfo* nextBatch = batch->next_;
            free ( batch );
            batch = nextBatch;
        }
    }

    /// allocate space for an array of objectPerAllocation object.
    /// @warning it is the responsability of the caller to call objects constructors.
    AllocatedType* allocate ()
    {
        if ( freeHead_ ) // returns node from free list.
        {
            AllocatedType* object = freeHead_;
            freeHead_ = * (AllocatedType**)object;
            return object;
        }

        if ( currentBatch_->used_ == currentBatch_->end_ )
        {
            currentBatch_ = currentBatch_->next_;

            while ( currentBatch_  &&  currentBatch_->used_ == currentBatch_->end_ )
                currentBatch_ = currentBatch_->next_;

            if ( !currentBatch_  ) // no free batch found, allocate a new one
            {
                currentBatch_ = allocateBatch ( objectsPerPage_ );
                currentBatch_->next_ = batches_; // insert at the head of the list
                batches_ = currentBatch_;
            }
        }

        AllocatedType* allocated = currentBatch_->used_;
        currentBatch_->used_ += objectPerAllocation;
        return allocated;
    }

    /// Release the object.
    /// @warning it is the responsability of the caller to actually destruct the object.
    void release ( AllocatedType* object )
    {
        assert ( object != 0 );
        * (AllocatedType**)object = freeHead_;
        freeHead_ = object;
    }

private:
    struct BatchInfo
    {
        BatchInfo* next_;
        AllocatedType* used_;
        AllocatedType* end_;
        AllocatedType buffer_[objectPerAllocation];
    };

    // disabled copy constructor and assignement operator.
    BatchAllocator ( const BatchAllocator& );
    void operator = ( const BatchAllocator&);

    static BatchInfo* allocateBatch ( unsigned int objectsPerPage )
    {
        const unsigned int mallocSize = sizeof (BatchInfo) - sizeof (AllocatedType) * objectPerAllocation
                                        + sizeof (AllocatedType) * objectPerAllocation * objectsPerPage;
        BatchInfo* batch = static_cast<BatchInfo*> ( malloc ( mallocSize ) );
        batch->next_ = 0;
        batch->used_ = batch->buffer_;
        batch->end_ = batch->buffer_ + objectsPerPage;
        return batch;
    }

    BatchInfo* batches_;
    BatchInfo* currentBatch_;
    /// Head of a single linked list within the allocated space of freeed object
    AllocatedType* freeHead_;
    unsigned int objectsPerPage_;
};


} // namespace Json

#endif // JSONCPP_BATCHALLOCATOR_H_INCLUDED

