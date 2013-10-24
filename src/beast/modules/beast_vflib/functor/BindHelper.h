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

#ifndef BEAST_VFLIB_BINDHELPER_H_INCLUDED
#define BEAST_VFLIB_BINDHELPER_H_INCLUDED

namespace beast {
	
/** Calls bind() for you.
 The UnaryFunction will be called with this signature:
 @code
 template <typename Functor>
 void operator() (Functor const& f);
 @endcode
 Where Functor is the result of the bind.
 */
template <class UnaryFunction>
class BindHelper
{
private:
	// Gets called with the bind
	UnaryFunction m_f;
	
public:
	template <typename Arg>
	explicit BindHelper (Arg& arg)
	: m_f (arg)
	{ }
	
	template <typename Arg>
	explicit BindHelper (Arg const& arg)
	: m_f (arg)
	{ }
	
	template <typename F>
	void operator() (F const& f) const
	{ m_f (f); }
	
	template <typename F, class P1>
	void operator() (F const& f, P1 const& p1) const
	{ m_f (bind (f, p1)); }
	
	template <typename F, class P1, class P2>
	void operator() (F const& f, P1 const& p1, P2 const& p2) const
	{ m_f (bind (f, p1, p2)); }
	
	template <typename F, class P1, class P2, class P3>
	void operator() (F const& f, P1 const& p1, P2 const& p2, P3 const& p3) const
	{ m_f (bind (f, p1, p2, p3)); }
	
	template <typename F, class P1, class P2, class P3, class P4>
	void operator() (F const& f, P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4) const
	{ m_f (bind (f, p1, p2, p3, p4)); }
	
	template <typename F, class P1, class P2, class P3, class P4, class P5>
	void operator() (F const& f, P1 const& p1, P2 const& p2, P3 const& p3, P4 const& p4, P5 const& p5) const
	{ m_f (bind (f, p1, p2, p3, p4, p5)); }
};

}

#endif
