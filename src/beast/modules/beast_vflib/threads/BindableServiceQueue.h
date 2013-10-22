//------------------------------------------------------------------------------
/*
	This file is part of Beast: https://github.com/vinniefalco/Beast
	Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_VFLIB_BINDABLESERVICEQUEUETYPE_H_INCLUDED
#define BEAST_VFLIB_BINDABLESERVICEQUEUETYPE_H_INCLUDED

#include "beast/Threads.h"
#include "../functor/BindHelper.h"

namespace beast {
	
	template <class Allocator = std::allocator <char> >
	class BindableServiceQueueType
	: public ServiceQueueType <Allocator>
	{
	public:
		explicit BindableServiceQueueType (int expectedConcurrency = 1,
										   Allocator alloc = Allocator())
		: ServiceQueueType<Allocator>(expectedConcurrency, alloc)
		, queue(*this)
		, call(*this)
		{
		}
		
		struct BindHelperPost
		{
			BindableServiceQueueType<Allocator>& m_queue;
			explicit BindHelperPost (BindableServiceQueueType<Allocator>& queue)
			: m_queue (queue)
			{ }
			template <typename F>
			void operator() (F const& f) const
			{ m_queue.post ( F (f) ); }
		};
		
		struct BindHelperDispatch
		{
			BindableServiceQueueType<Allocator>& m_queue;
			explicit BindHelperDispatch (BindableServiceQueueType<Allocator>& queue)
			: m_queue (queue)
			{ }
			template <typename F>
			void operator() (F const& f) const
			{ m_queue.dispatch ( F (f) ); }
		};
		
		BindHelper <BindHelperPost> const queue;
		BindHelper <BindHelperDispatch> const call;
	};
	
	typedef BindableServiceQueueType <std::allocator <char> > BindableServiceQueue;
	
}

#endif
