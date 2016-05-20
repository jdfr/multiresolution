#ifndef SERIALIZATION_HEADER
#define SERIALIZATION_HEADER

#include <vector>
#include <map>
#include <type_traits>


/*VERY BASIC SERIALIZATION FRAMEWORK
 * it can handle tree-like structures, but not anything more complicated such as DAGs implemented with shared_ptr
   (note, however, that in some cases, cross-references within trees can be trivially reconstructed)*/

///////////////////////////////////////////
// these templates are handy for declaring list of serialized variables
template<typename... Args> void   serialize_all(FILE * f, Args&... args) { char dummy[sizeof...(Args)] = { (  serialize(f, args), (char)0)... }; }
template<typename... Args> void deserialize_all(FILE * f, Args&... args) { char dummy[sizeof...(Args)] = { (deserialize(f, args), (char)0)... }; }

///////////////////////////////////////////
// detect if an object has a serialization_object() method
template<typename T> struct has_serialization {
private:
    template<typename A, A, typename B, B> class check {};

    template<typename C> static char f(check<void (C::*)(FILE *), &C::serialize_object, void (C::*)(FILE *), &C::deserialize_object> *);
    template<typename C> static long f(...);

public:
    static const bool value = (sizeof(f<T>(nullptr)) == sizeof(char));
};

///////////////////////////////////////////
// DEFAULT SERIALIZATION (FOR BASIC TYPES)
// (primitive types and structs containing only primitive types)
// in a previous implementation, we used a explicitly defined has_overloaded_serialization for each type.
// This approach is simpler, but will fail silently if we try to serialize trivial structs with raw pointers or arrays!
template<typename T> typename std::enable_if<std::is_trivially_copyable<T>::value, void>::type   serialize(FILE *f, T &data) { if (fwrite(&data, sizeof(data), 1, f) != 1) throw std::runtime_error("Serialization error!"); }
template<typename T> typename std::enable_if<std::is_trivially_copyable<T>::value, void>::type deserialize(FILE *f, T &data) { if (fread (&data, sizeof(data), 1, f) != 1) throw std::runtime_error("Serialization error!"); }

///////////////////////////////////////////
// SERIALIZATION FOR OBJECTS THAT IMPLEMENT serialize_object()
template<typename T> typename std::enable_if<has_serialization<T>::value, void>::type   serialize(FILE *f, T &data) { data.  serialize_object(f); }
template<typename T> typename std::enable_if<has_serialization<T>::value, void>::type deserialize(FILE *f, T &data) { data.deserialize_object(f); }

///////////////////////////////////////////
// SERIALIZATION FOR std::shared_ptr
template<typename T> void   serialize(FILE *f, std::shared_ptr<T> &data) {                                 serialize(f, *data); }
template<typename T> void deserialize(FILE *f, std::shared_ptr<T> &data) { data = std::make_shared<T>(); deserialize(f, *data); }

///////////////////////////////////////////
// SERIALIZATION FOR std::vector
// (optimized vectors of basic types)
template<typename T> typename std::enable_if<!std::is_trivially_copyable<T>::value, void>::type   serialize(FILE *f, std::vector<T> &data) {
    size_t numdata = data.size();
    serialize(f, numdata);
    for (auto &d : data) serialize(f, d);
}

template<typename T> typename std::enable_if<!std::is_trivially_copyable<T>::value, void>::type deserialize(FILE *f, std::vector<T> &data, T sample = T()) {
    size_t numdata;
    deserialize(f, numdata);
    data.clear();
    data.reserve(numdata);
    for (size_t i = 0; i < numdata; ++i) {
        deserialize(f, sample);
        data.push_back(std::move(sample));
    }
}

template<typename T> typename std::enable_if<std::is_trivially_copyable<T>::value, void>::type   serialize(FILE *f, std::vector<T> &data) {
    size_t numdata = data.size();
    serialize(f, numdata);
    if (numdata>0) if (fwrite(&data.front(), sizeof(T), numdata, f) != numdata) throw std::runtime_error("Serialization error!");
}

template<typename T> typename std::enable_if<std::is_trivially_copyable<T>::value, void>::type deserialize(FILE *f, std::vector<T> &data) {
    size_t numdata;
    deserialize(f, numdata);
    data.resize(numdata);
    if (numdata>0) if (fread(&data.front(), sizeof(T), numdata, f) != numdata) throw std::runtime_error("Serialization error!");
}


///////////////////////////////////////////
// SERIALIZATION FOR std::map
template<typename K, typename V> void   serialize(FILE *f, std::map<K, V> &data) {
    size_t numdata = data.size();
    serialize(f, numdata);
    for (auto &pair : data) {
        serialize(f, pair.first);
        serialize(f, pair.second);
    }
}

template<typename K, typename V> void deserialize(FILE *f, std::map<K, V> &data) {
    size_t numdata;
    deserialize(f, numdata);
    data.clear();
    for (size_t i = 0; i < numdata; ++i) {
        K key;
        V value;
        deserialize(f, key);
        deserialize(f, value);
        data.emplace(std::move(key), std::move(value));
    }
}

//Variadic macro wizardry can be used to automate the declaration of serialized variables,
//but MSVC does not accept it, so we are stuck with horrible, error-prone, explicit, hand-made lists.
//On the other hand, automating the declaration of serialization lists was ugly as hell, so whatever...

/*NOTE: ideally, we should not need to explicitly mention all member variables while serializing a class,
 * but we should be able to serialize all at once, and only handle explicitly complex cases:
 * 
 *      void Class::serialize_object(FILE *f) {
 *          fwrite(this, sizeof(Class), 1, f); //write everything at once
 *          serialize_all(f, this->member_vector, this->member_object);
 *     }
 *      void Class::serialize_object(FILE *f) {
 *          fread(this, sizeof(Class), 1, f); //write everything at once
 *          deserialize_all(f, this->member_vector, this->member_object);
 *     }
 * 
 * The problem here is that in general, trivially serialized STL containers will be inconsistent when deserialized,
 * so they will certainly provoke a segfault when deserialize_all() tries to modify them. That STL containers may have
 * different sizes in Debug and Release modes is just the icing on the cake.
 */

#define SERIALIZATION_DEFINITION(...) \
    void   serialize_object(FILE *f) {   serialize_all(f, __VA_ARGS__); } \
    void deserialize_object(FILE *f) { deserialize_all(f, __VA_ARGS__); }

#define SERIALIZATION_CUSTOM_DEFINITION(SERIALIZE_CUSTOM, DESERIALIZE_CUSTOM, POST_DESERIALIZE_CUSTOM, ...) \
    void   serialize_object(FILE *f) { SERIALIZE_CUSTOM;     serialize_all(f, __VA_ARGS__); } \
    void deserialize_object(FILE *f) { DESERIALIZE_CUSTOM; deserialize_all(f, __VA_ARGS__); POST_DESERIALIZE_CUSTOM; }


/*variadic macro wizardry: define macro DECLARE_SERIALIZABLE_VARIABLES
 * with two initial arguments and then a variadic list of arguments
 * TYPE_1, NAME_1, TYPE_2, NAME_2, ..., TYPE_N, NAME_N. Then, the macro:
 *     - declares each variable NAME_I with type TYPE_I
 *     - declares methods serialize_object(FILE *f) and deserialize_object(FILE *f),
 *       such that all variables are serialized / deserialized
 *     - the previously mentioned two initial arguments are used to declare
 *       serialization of variables which cannot be expressed in the list
 * */

/*
template<typename T> struct argument_type;
template<typename T, typename U> struct argument_type<T(U)> { typedef U type; };

#define EVAL0(...) __VA_ARGS__
#define EVAL1(...) EVAL0 (EVAL0 (EVAL0 (__VA_ARGS__)))
#define EVAL2(...) EVAL1 (EVAL1 (EVAL1 (__VA_ARGS__)))
#define EVAL3(...) EVAL2 (EVAL2 (EVAL2 (__VA_ARGS__)))
#define EVAL4(...) EVAL3 (EVAL3 (EVAL3 (__VA_ARGS__)))
#define EVAL(...)  EVAL4 (EVAL4 (EVAL4 (__VA_ARGS__)))

#define MAP_END(...)
#define MAP_OUT

#define MAP_GET_END2() 0, MAP_END
#define MAP_GET_END1(...) MAP_GET_END2
#define MAP_GET_END(...) MAP_GET_END1

#define MAP_NEXT0(test, next, ...) next MAP_OUT
#define MAP_NEXT1(test, next) MAP_NEXT0 (test, next, 0)
#define MAP_NEXT(test, next)  MAP_NEXT1 (MAP_GET_END test, next)

#define MAP0(f, x, y, peek, ...) f(x, y) MAP_NEXT (peek, MAP1) (f, peek, __VA_ARGS__)
#define MAP1(f, x, y, peek, ...) f(x, y) MAP_NEXT (peek, MAP0) (f, peek, __VA_ARGS__)
#define MAP(f, ...) EVAL (MAP1 (f, __VA_ARGS__, ()()(), ()()(), ()()(), 0))

#define     DECLARE_VARIABLE(TYPE, NAME) argument_type<void(TYPE)>::type NAME;
#define   SERIALIZE_VARIABLE(TYPE, NAME)   serialize(f, NAME);
#define DESERIALIZE_VARIABLE(TYPE, NAME) deserialize(f, NAME);

#define DECLARE_SERIALIZABLE_VARIABLES(SERIALIZE_CUSTOM, DESERIALIZE_CUSTOM, ...) \
    MAP(DECLARE_VARIABLE, __VA_ARGS__) \
    void   serialize_object(FILE *f) {   SERIALIZE_CUSTOM; MAP(  SERIALIZE_VARIABLE, __VA_ARGS__) } \
    void deserialize_object(FILE *f) { DESERIALIZE_CUSTOM; MAP(DESERIALIZE_VARIABLE, __VA_ARGS__) }
*/

#endif
