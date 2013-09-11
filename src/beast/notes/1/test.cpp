/*
    This code normally fails to compile under Visual Studio 2012

    The fix is marked with _MSC_VER
*/

#include <iostream>
#include <typeinfo>

template <class T> struct has_lowest_layer_type {
  typedef char yes; 
  typedef struct {char dummy[2];} no; 
  template <class C> static yes f(typename C::lowest_layer_type*); 
  template <class C> static no f(...); 
#ifdef _MSC_VER
  static const bool value = sizeof(f<T>(0)) == 1;
#else
  static const bool value = sizeof(has_lowest_layer_type<T>::f<T>(0)) == 1;
#endif
}; 

template <bool Enable>
struct EnableIf : std::false_type { };

template <>
struct EnableIf <true> : std::true_type { };

struct tcp { };

template <class Protocol>
struct basic_socket
{
    typedef basic_socket <Protocol> lowest_layer_type;
};

template <class Protocol>
struct basic_stream_socket : basic_socket <Protocol>
{
    typedef basic_socket <Protocol> next_layer_type;
};

struct A
{
    typedef basic_socket <tcp> lowest_layer_type;
};

struct B
{
};

template <typename T>
void show (std::true_type)
{
      std::cout << typeid(T).name() << " has lowest_layer_type" << std::endl;
}
 
template <typename T>
void show (std::false_type)
{
      std::cout << typeid(T).name() << " does not have lowest_layer_type" << std::endl;
}
 
template <typename T>
void show ()
{
  show <T> (EnableIf <has_lowest_layer_type <T>::value> ());
}
 
int main ()
{
  show <A> ();
  show <B> ();
  show <basic_socket <tcp> > ();
  show <basic_stream_socket <tcp> > ();
  return 0;
}
/*
1>..\..\Subtrees\beast\notes\1\test.cpp(16): error C2783: 'has_lowest_layer_type<T>::no has_lowest_layer_type<T>::f(...)' : could not deduce template argument for 'C'
1>          with
1>          [
1>              T=A
1>          ]
1>          ..\..\Subtrees\beast\notes\1\test.cpp(15) : see declaration of 'has_lowest_layer_type<T>::f'
1>          with
1>          [
1>              T=A
1>          ]
1>          ..\..\Subtrees\beast\notes\1\test.cpp(63) : see reference to class template instantiation 'has_lowest_layer_type<T>' being compiled
1>          with
1>          [
1>              T=A
1>          ]
1>          ..\..\Subtrees\beast\notes\1\test.cpp(68) : see reference to function template instantiation 'void show<A>(void)' being compiled
*/
