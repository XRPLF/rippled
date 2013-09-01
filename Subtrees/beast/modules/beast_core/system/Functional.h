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

#ifndef BEAST_FUNCTIONAL_H_INCLUDED
#define BEAST_FUNCTIONAL_H_INCLUDED

//------------------------------------------------------------------------------

// inspired by Roman Perepelitsa's presentation from comp.lang.c++.moderated
// based on the implementation here: http://www.rsdn.ru/forum/cpp/2759773.1.aspx
//
namespace is_call_possible_detail
{
    template<typename T>
    struct add_reference
    {
      typedef T& type;
    };

    template<typename T>
    struct add_reference<T&>
    {
      typedef T& type;
    };

   template <typename T> class void_exp_result {}; 

   template <typename T, typename U> 
   U const& operator,(U const&, void_exp_result<T>); 

   template <typename T, typename U> 
   U& operator,(U&, void_exp_result<T>); 

   template <typename src_type, typename dest_type> 
   struct clone_constness 
   { 
     typedef dest_type type; 
   }; 

   template <typename src_type, typename dest_type> 
   struct clone_constness<const src_type, dest_type> 
   { 
     typedef const dest_type type; 
   }; 
}

#define BEAST_DEFINE_HAS_MEMBER_FUNCTION(trait_name, member_function_name)                                   \
template<typename T, typename Signature> class trait_name;                                                   \
                                                                                                             \
template<typename T, typename Result>                                                                        \
class trait_name<T, Result(void)>                                                                            \
{                                                                                                            \
   class yes { char m; };                                                                                    \
   class no { yes m[2]; };                                                                                   \
   struct base_mixin                                                                                         \
   {                                                                                                         \
     Result member_function_name();                                                                          \
   };                                                                                                        \
   struct base : public T, public base_mixin { private: base(); };                                           \
   template <typename U, U t>  class helper{};                                                               \
   template <typename U>                                                                                     \
   static no deduce(U*, helper<Result (base_mixin::*)(), &U::member_function_name>* = 0);                    \
   static yes deduce(...);                                                                                   \
public:                                                                                                      \
   static const bool value = sizeof(yes) == sizeof(deduce(static_cast<base*>(0)));                           \
};                                                                                                           \
                                                                                                             \
template<typename T, typename Result, typename Arg>                                                          \
class trait_name<T, Result(Arg)>                                                                             \
{                                                                                                            \
   class yes { char m; };                                                                                    \
   class no { yes m[2]; };                                                                                   \
   struct base_mixin                                                                                         \
   {                                                                                                         \
     Result member_function_name(Arg);                                                                       \
   };                                                                                                        \
   struct base : public T, public base_mixin { private: base(); };                                           \
   template <typename U, U t>  class helper{};                                                               \
   template <typename U>                                                                                     \
   static no deduce(U*, helper<Result (base_mixin::*)(Arg), &U::member_function_name>* = 0);                 \
   static yes deduce(...);                                                                                   \
public:                                                                                                      \
   static const bool value = sizeof(yes) == sizeof(deduce(static_cast<base*>(0)));                           \
};                                                                                                           \
                                                                                                             \
template<typename T, typename Result, typename Arg1, typename Arg2>                                          \
class trait_name<T, Result(Arg1,Arg2)>                                                                       \
{                                                                                                            \
   class yes { char m; };                                                                                    \
   class no { yes m[2]; };                                                                                   \
   struct base_mixin                                                                                         \
   {                                                                                                         \
     Result member_function_name(Arg1,Arg2);                                                                 \
   };                                                                                                        \
   struct base : public T, public base_mixin { private: base(); };                                           \
   template <typename U, U t>  class helper{};                                                               \
   template <typename U>                                                                                     \
   static no deduce(U*, helper<Result (base_mixin::*)(Arg1,Arg2), &U::member_function_name>* = 0);           \
   static yes deduce(...);                                                                                   \
public:                                                                                                      \
   static const bool value = sizeof(yes) == sizeof(deduce(static_cast<base*>(0)));                           \
};                                                                                                           \
                                                                                                             \
template<typename T, typename Result, typename Arg1, typename Arg2, typename Arg3>                           \
class trait_name<T, Result(Arg1,Arg2,Arg3)>                                                                  \
{                                                                                                            \
   class yes { char m; };                                                                                    \
   class no { yes m[2]; };                                                                                   \
   struct base_mixin                                                                                         \
   {                                                                                                         \
     Result member_function_name(Arg1,Arg2,Arg3);                                                            \
   };                                                                                                        \
   struct base : public T, public base_mixin { private: base(); };                                           \
   template <typename U, U t>  class helper{};                                                               \
   template <typename U>                                                                                     \
   static no deduce(U*, helper<Result (base_mixin::*)(Arg1,Arg2,Arg3), &U::member_function_name>* = 0);      \
   static yes deduce(...);                                                                                   \
public:                                                                                                      \
   static const bool value = sizeof(yes) == sizeof(deduce(static_cast<base*>(0)));                           \
};                                                                                                           \
                                                                                                             \
template<typename T, typename Result, typename Arg1, typename Arg2, typename Arg3, typename Arg4>            \
class trait_name<T, Result(Arg1,Arg2,Arg3,Arg4)>                                                             \
{                                                                                                            \
   class yes { char m; };                                                                                    \
   class no { yes m[2]; };                                                                                   \
   struct base_mixin                                                                                         \
   {                                                                                                         \
     Result member_function_name(Arg1,Arg2,Arg3,Arg4);                                                       \
   };                                                                                                        \
   struct base : public T, public base_mixin { private: base(); };                                           \
   template <typename U, U t>  class helper{};                                                               \
   template <typename U>                                                                                     \
   static no deduce(U*, helper<Result (base_mixin::*)(Arg1,Arg2,Arg3,Arg4), &U::member_function_name>* = 0); \
   static yes deduce(...);                                                                                   \
public:                                                                                                      \
   static const bool value = sizeof(yes) == sizeof(deduce(static_cast<base*>(0)));                           \
}                                                                                                           

#define BEAST_DEFINE_IS_CALL_POSSIBLE(trait_name, member_function_name)                                                 \
struct trait_name##_detail                                                                                              \
{                                                                                                                       \
BEAST_DEFINE_HAS_MEMBER_FUNCTION(has_member, member_function_name);                                                     \
};                                                                                                                      \
                                                                                                                        \
template <typename T, typename Signature>                                                                               \
struct trait_name                                                                                                       \
{                                                                                                                       \
  private:                                                                                                              \
   class yes {};                                                                                                        \
   class no { yes m[2]; };                                                                                              \
   struct derived : public T                                                                                            \
   {                                                                                                                    \
     using T::member_function_name;                                                                                     \
     no member_function_name(...) const;                                                                                \
     private: derived ();                                                                                               \
   };                                                                                                                   \
                                                                                                                        \
   typedef typename is_call_possible_detail::clone_constness<T, derived>::type derived_type;                            \
                                                                                                                        \
   template <typename U, typename Result>                                                                               \
   struct return_value_check                                                                                            \
   {                                                                                                                    \
     static yes deduce(Result);                                                                                         \
     static no deduce(...);                                                                                             \
     static no deduce(no);                                                                                              \
     static no deduce(is_call_possible_detail::void_exp_result<T>);                                                     \
   };                                                                                                                   \
                                                                                                                        \
   template <typename U>                                                                                                \
   struct return_value_check<U, void>                                                                                   \
   {                                                                                                                    \
     static yes deduce(...);                                                                                            \
     static no deduce(no);                                                                                              \
   };                                                                                                                   \
                                                                                                                        \
   template <bool has_the_member_of_interest, typename F>                                                               \
   struct impl                                                                                                          \
   {                                                                                                                    \
     static const bool value = false;                                                                                   \
   };                                                                                                                   \
                                                                                                                        \
   template <typename Result, typename Arg>                                                                             \
   struct impl<true, Result(Arg)>                                                                                       \
   {                                                                                                                    \
     static typename is_call_possible_detail::add_reference<derived_type>::type test_me;                                \
     static typename is_call_possible_detail::add_reference<Arg>::type          arg;                                    \
                                                                                                                        \
     static const bool value =                                                                                          \
       sizeof(                                                                                                          \
            return_value_check<T, Result>::deduce(                                                                      \
             (test_me.member_function_name(arg), is_call_possible_detail::void_exp_result<T>())                         \
                         )                                                                                              \
            ) == sizeof(yes);                                                                                           \
   };                                                                                                                   \
                                                                                                                        \
   template <typename Result, typename Arg1, typename Arg2>                                                             \
   struct impl<true, Result(Arg1,Arg2)>                                                                                 \
   {                                                                                                                    \
     static typename is_call_possible_detail::add_reference<derived_type>::type test_me;                                \
     static typename is_call_possible_detail::add_reference<Arg1>::type         arg1;                                   \
     static typename is_call_possible_detail::add_reference<Arg2>::type         arg2;                                   \
                                                                                                                        \
     static const bool value =                                                                                          \
       sizeof(                                                                                                          \
            return_value_check<T, Result>::deduce(                                                                      \
             (test_me.member_function_name(arg1,arg2), is_call_possible_detail::void_exp_result<T>())                   \
                         )                                                                                              \
            ) == sizeof(yes);                                                                                           \
   };                                                                                                                   \
                                                                                                                        \
   template <typename Result, typename Arg1, typename Arg2, typename Arg3>                                              \
   struct impl<true, Result(Arg1,Arg2,Arg3)>                                                                            \
   {                                                                                                                    \
     static typename is_call_possible_detail::add_reference<derived_type>::type test_me;                                \
     static typename is_call_possible_detail::add_reference<Arg1>::type         arg1;                                   \
     static typename is_call_possible_detail::add_reference<Arg2>::type         arg2;                                   \
     static typename is_call_possible_detail::add_reference<Arg3>::type         arg3;                                   \
                                                                                                                        \
     static const bool value =                                                                                          \
       sizeof(                                                                                                          \
            return_value_check<T, Result>::deduce(                                                                      \
             (test_me.member_function_name(arg1,arg2,arg3), is_call_possible_detail::void_exp_result<T>())              \
                         )                                                                                              \
            ) == sizeof(yes);                                                                                           \
   };                                                                                                                   \
                                                                                                                        \
   template <typename Result, typename Arg1, typename Arg2, typename Arg3, typename Arg4>                               \
   struct impl<true, Result(Arg1,Arg2,Arg3,Arg4)>                                                                       \
   {                                                                                                                    \
     static typename is_call_possible_detail::add_reference<derived_type>::type test_me;                                \
     static typename is_call_possible_detail::add_reference<Arg1>::type         arg1;                                   \
     static typename is_call_possible_detail::add_reference<Arg2>::type         arg2;                                   \
     static typename is_call_possible_detail::add_reference<Arg3>::type         arg3;                                   \
     static typename is_call_possible_detail::add_reference<Arg4>::type         arg4;                                   \
                                                                                                                        \
     static const bool value =                                                                                          \
       sizeof(                                                                                                          \
            return_value_check<T, Result>::deduce(                                                                      \
             (test_me.member_function_name(arg1,arg2,arg3,arg4), is_call_possible_detail::void_exp_result<T>())         \
                         )                                                                                              \
            ) == sizeof(yes);                                                                                           \
   };                                                                                                                   \
                                                                                                                        \
  public:                                                                                                               \
    static const bool value = impl<trait_name##_detail::has_member<T,Signature>::value, Signature>::value;              \
}

//------------------------------------------------------------------------------

/*  Brings functional support into our namespace, based on environment.

    Notes on bind

    Difference between boost::bind and std::bind
    http://stackoverflow.com/questions/10555566/is-there-any-difference-between-c11-stdbind-and-boostbind

    Resolving conflict between boost::shared_ptr and std::shared_ptr
    http://stackoverflow.com/questions/4682343/how-to-resolve-conflict-between-boostshared-ptr-and-using-stdshared-ptr
*/ 

#ifndef BEAST_BIND_PLACEHOLDERS_N
# if BEAST_MSVC && BEAST_FUNCTIONAL_USES_STD
#  define BEAST_BIND_PLACEHOLDERS_N 20 // Visual Studio 2012
# else
#  define BEAST_BIND_PLACEHOLDERS_N 8 // Seems a reasonable number
# endif
#endif

/** Max number of arguments to bind, total.
*/
#if BEAST_MSVC
# ifdef _VARIADIC_MAX
#  define BEAST_VARIADIC_MAX _VARIADIC_MAX
# else
#  define BEAST_VARIADIC_MAX 9
# endif
#else
# define BEAST_VARIADIC_MAX 9
#endif

//------------------------------------------------------------------------------

#if BEAST_FUNCTIONAL_USES_STD

namespace functional
{

using std::ref;
using std::cref;
using std::bind;
using std::function;

}

using namespace functional;

namespace placeholders
{

#if BEAST_BIND_PLACEHOLDERS_N >= 1
using std::placeholders::_1;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 2
using std::placeholders::_2;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 3
using std::placeholders::_3;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 4
using std::placeholders::_4;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 5
using std::placeholders::_5;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 6
using std::placeholders::_6;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 7
using std::placeholders::_7;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 8
using std::placeholders::_8;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 9
using std::placeholders::_9;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 10
using std::placeholders::_10;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 11
using std::placeholders::_11;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 12
using std::placeholders::_12;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 13
using std::placeholders::_13;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 14
using std::placeholders::_14;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 15
using std::placeholders::_15;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 16
using std::placeholders::_16;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 17
using std::placeholders::_17;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 18
using std::placeholders::_18;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 19
using std::placeholders::_19;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 20
using std::placeholders::_20;
#endif

}

using namespace placeholders;

//------------------------------------------------------------------------------

#elif BEAST_FUNCTIONAL_USES_TR1

namespace functional
{

using std::tr1::ref;
using std::tr1::cref;
using std::tr1::bind;
using std::tr1::function;

}

using namespace functional;

namespace placeholders
{

#if BEAST_BIND_PLACEHOLDERS_N >= 1
using std::tr1::placeholders::_1;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 2
using std::tr1::placeholders::_2;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 3
using std::tr1::placeholders::_3;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 4
using std::tr1::placeholders::_4;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 5
using std::tr1::placeholders::_5;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 6
using std::tr1::placeholders::_6;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 7
using std::tr1::placeholders::_7;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 8
using std::tr1::placeholders::_8;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 9
using std::tr1::placeholders::_9;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 10
using std::tr1::placeholders::_10;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 11
using std::tr1::placeholders::_11;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 12
using std::tr1::placeholders::_12;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 13
using std::tr1::placeholders::_13;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 14
using std::tr1::placeholders::_14;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 15
using std::tr1::placeholders::_15;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 16
using std::tr1::placeholders::_16;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 17
using std::tr1::placeholders::_17;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 18
using std::tr1::placeholders::_18;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 19
using std::tr1::placeholders::_19;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 20
using std::tr1::placeholders::_20;
#endif

}

using namespace placeholders;

//------------------------------------------------------------------------------

#elif BEAST_FUNCTIONAL_USES_BOOST

namespace functional
{

using boost::ref;
using boost::cref;
using boost::bind;
using boost::function;

}

using namespace functional;

namespace placeholders
{

#if BEAST_BIND_PLACEHOLDERS_N >= 1
using boost::placeholders::_1;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 2
using boost::placeholders::_2;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 3
using boost::placeholders::_3;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 4
using boost::placeholders::_4;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 5
using boost::placeholders::_5;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 6
using boost::placeholders::_6;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 7
using boost::placeholders::_7;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 8
using boost::placeholders::_8;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 9
using boost::placeholders::_9;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 10
using boost::placeholders::_10;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 11
using boost::placeholders::_11;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 12
using boost::placeholders::_12;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 13
using boost::placeholders::_13;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 14
using boost::placeholders::_14;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 15
using boost::placeholders::_15;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 16
using boost::placeholders::_16;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 17
using boost::placeholders::_17;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 18
using boost::placeholders::_18;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 19
using boost::placeholders::_19;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 20
using boost::placeholders::_20;
#endif

}

using namespace placeholders;

//------------------------------------------------------------------------------

#else

#error Unknown bind source in Functional.h

#endif

#endif
