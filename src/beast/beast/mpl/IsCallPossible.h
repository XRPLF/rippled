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

#ifndef BEAST_MPL_ISCALLPOSSIBLE_H_INCLUDED
#define BEAST_MPL_ISCALLPOSSIBLE_H_INCLUDED

namespace beast {
namespace mpl {

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
template<typename T, typename IsCallPossibleSignature> class trait_name;                                     \
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
template <typename T, typename IsCallPossibleSignature>                                                                 \
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
   typedef typename beast::mpl::is_call_possible_detail::clone_constness<T, derived>::type derived_type;                \
                                                                                                                        \
   template <typename U, typename Result>                                                                               \
   struct return_value_check                                                                                            \
   {                                                                                                                    \
     static yes deduce(Result);                                                                                         \
     static no deduce(...);                                                                                             \
     static no deduce(no);                                                                                              \
     static no deduce(beast::mpl::is_call_possible_detail::void_exp_result<T>);                                         \
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
     static typename beast::mpl::is_call_possible_detail::add_reference<derived_type>::type test_me;                    \
     static typename beast::mpl::is_call_possible_detail::add_reference<Arg>::type          arg;                        \
                                                                                                                        \
     static const bool value =                                                                                          \
       sizeof(                                                                                                          \
            return_value_check<T, Result>::deduce(                                                                      \
             (test_me.member_function_name(arg), beast::mpl::is_call_possible_detail::void_exp_result<T>())             \
                         )                                                                                              \
            ) == sizeof(yes);                                                                                           \
   };                                                                                                                   \
                                                                                                                        \
   template <typename Result, typename Arg1, typename Arg2>                                                             \
   struct impl<true, Result(Arg1,Arg2)>                                                                                 \
   {                                                                                                                    \
     static typename beast::mpl::is_call_possible_detail::add_reference<derived_type>::type test_me;                    \
     static typename beast::mpl::is_call_possible_detail::add_reference<Arg1>::type         arg1;                       \
     static typename beast::mpl::is_call_possible_detail::add_reference<Arg2>::type         arg2;                       \
                                                                                                                        \
     static const bool value =                                                                                          \
       sizeof(                                                                                                          \
            return_value_check<T, Result>::deduce(                                                                      \
             (test_me.member_function_name(arg1,arg2), beast::mpl::is_call_possible_detail::void_exp_result<T>())       \
                         )                                                                                              \
            ) == sizeof(yes);                                                                                           \
   };                                                                                                                   \
                                                                                                                        \
   template <typename Result, typename Arg1, typename Arg2, typename Arg3>                                              \
   struct impl<true, Result(Arg1,Arg2,Arg3)>                                                                            \
   {                                                                                                                    \
     static typename beast::mpl::is_call_possible_detail::add_reference<derived_type>::type test_me;                    \
     static typename beast::mpl::is_call_possible_detail::add_reference<Arg1>::type         arg1;                       \
     static typename beast::mpl::is_call_possible_detail::add_reference<Arg2>::type         arg2;                       \
     static typename beast::mpl::is_call_possible_detail::add_reference<Arg3>::type         arg3;                       \
                                                                                                                        \
     static const bool value =                                                                                          \
       sizeof(                                                                                                          \
            return_value_check<T, Result>::deduce(                                                                      \
             (test_me.member_function_name(arg1,arg2,arg3), beast::mpl::is_call_possible_detail::void_exp_result<T>())  \
                         )                                                                                              \
            ) == sizeof(yes);                                                                                           \
   };                                                                                                                   \
                                                                                                                        \
   template <typename Result, typename Arg1, typename Arg2, typename Arg3, typename Arg4>                               \
   struct impl<true, Result(Arg1,Arg2,Arg3,Arg4)>                                                                       \
   {                                                                                                                    \
     static typename beast::mpl::is_call_possible_detail::add_reference<derived_type>::type test_me;                    \
     static typename beast::mpl::is_call_possible_detail::add_reference<Arg1>::type         arg1;                       \
     static typename beast::mpl::is_call_possible_detail::add_reference<Arg2>::type         arg2;                       \
     static typename beast::mpl::is_call_possible_detail::add_reference<Arg3>::type         arg3;                       \
     static typename beast::mpl::is_call_possible_detail::add_reference<Arg4>::type         arg4;                       \
                                                                                                                        \
     static const bool value =                                                                                          \
       sizeof(                                                                                                          \
            return_value_check<T, Result>::deduce(                                                                      \
             (test_me.member_function_name(arg1,arg2,arg3,arg4),                                                        \
                beast::mpl::is_call_possible_detail::void_exp_result<T>())                                              \
                         )                                                                                              \
            ) == sizeof(yes);                                                                                           \
   };                                                                                                                   \
                                                                                                                        \
  public:                                                                                                               \
    static const bool value = impl<trait_name##_detail::template has_member<T,IsCallPossibleSignature>::value,          \
        IsCallPossibleSignature>::value;                                                                                \
}

}
}

#endif
