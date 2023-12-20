The lock-free part of this code needs to be carefully audited. But note the memory savings do not depend on the lock-free nature. We can re-add the locks and still get the memory savings.

Making the inner node lock free may actually be _slower_ then using locks. There are operations that now use atomics that didn't use atomics before. This can be addressed by either re-adding the locks or potentially modifying this patch so only code that used to be under locks use atomic operations, and code that was not under locks do not use atomics.

There is a total of 32-bits used for pointer accounting information: 16-bits for the strong count; 14-bits for the weak count, 1 bit if "partial delete" was started, and 1 bit if "partial delete" has finished. I need to audit the code to make sure these limits are reasonable. The total bits can easily be bumped up to 64 without _too_ much impact on the memory savings if needed. We can also reallocate more bits to the strong count and less bits to the weak count if an audit shows that is better (I suspect there are far fewer weak pointers than strong pointers).

Much of the complication of the intrusive pointer comes from supporting weak pointers (needed for the tagged cache). If we removed weak pointers from the tagged cache this design gets much simpler.

This code will need substantially more testing (the unit tests are minimal right now). Unit tests that exercise various threading scenarios are particular important.

Note that this same technique can be used for other objects current kept in tagged caches. After we do this for inner nodes (this patch) we should see if it makes sense to do it for other objects as well.

