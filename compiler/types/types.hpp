#pragma once
#include <string>
#include <utility>
#include <vector>

namespace ipx {

// Scalar types are 0..15. Every other type is an interned compound type with id >= 16.
enum class Type : int { Int = 0, Float, String, Bool, Char, Void, Error, EmptyArray, EmptyBrace };

enum class Kind { Array, Map, Set, Heap, MaxHeap, Deque, Struct };

struct TypeInfo {
  Kind kind = Kind::Array;
  Type a = Type::Error;  // element type, or the key type of a map
  Type b = Type::Error;  // value type of a map
  std::string name;      // struct name
  std::vector<std::pair<std::string, Type>> fields;  // struct fields (filled by the checker)
};

inline std::vector<TypeInfo>& typeTable() {
  static std::vector<TypeInfo> table;
  return table;
}
inline bool isCompound(Type t) { return (int)t >= 16; }
inline const TypeInfo& info(Type t) { return typeTable()[(size_t)t - 16]; }
inline TypeInfo& infoMut(Type t) { return typeTable()[(size_t)t - 16]; }
inline bool isKind(Type t, Kind k) { return isCompound(t) && info(t).kind == k; }

inline bool validElem(Type t) {
  return t != Type::Error && t != Type::Void && t != Type::EmptyArray && t != Type::EmptyBrace;
}

inline Type makeType(Kind k, Type a, Type b = Type::Error) {
  auto& tab = typeTable();
  if (k != Kind::Struct)
    for (size_t i = 0; i < tab.size(); ++i)
      if (tab[i].kind == k && tab[i].a == a && tab[i].b == b) return (Type)(i + 16);
  TypeInfo ti;
  ti.kind = k;
  ti.a = a;
  ti.b = b;
  tab.push_back(ti);
  return (Type)(tab.size() - 1 + 16);
}

inline Type newStruct(const std::string& name) {  // never shared between programs
  Type t = makeType(Kind::Struct, Type::Error);
  infoMut(t).name = name;
  return t;
}

inline Type arrayOf(Type t) { return validElem(t) ? makeType(Kind::Array, t) : Type::Error; }
inline Type setOf(Type t) { return validElem(t) ? makeType(Kind::Set, t) : Type::Error; }
inline Type dequeOf(Type t) { return validElem(t) ? makeType(Kind::Deque, t) : Type::Error; }
inline Type heapOf(Type t, bool max) {
  return validElem(t) ? makeType(max ? Kind::MaxHeap : Kind::Heap, t) : Type::Error;
}
inline Type mapOf(Type k, Type v) {
  return (validElem(k) && validElem(v)) ? makeType(Kind::Map, k, v) : Type::Error;
}

inline bool isArray(Type t) { return isKind(t, Kind::Array); }
inline bool isMap(Type t) { return isKind(t, Kind::Map); }
inline bool isSet(Type t) { return isKind(t, Kind::Set); }
inline bool isDeque(Type t) { return isKind(t, Kind::Deque); }
inline bool isStruct(Type t) { return isKind(t, Kind::Struct); }
inline bool isHeap(Type t) { return isKind(t, Kind::Heap) || isKind(t, Kind::MaxHeap); }
inline bool isMaxHeap(Type t) { return isKind(t, Kind::MaxHeap); }
inline Type elemOf(Type t) { return info(t).a; }  // arrays, sets, heaps, deques
inline Type keyOf(Type t) { return info(t).a; }
inline Type valueOf(Type t) { return info(t).b; }

inline std::string typeName(Type t) {
  if (isCompound(t)) {
    const TypeInfo& ti = info(t);
    switch (ti.kind) {
      case Kind::Array: return typeName(ti.a) + "[]";
      case Kind::Map: return "map[" + typeName(ti.a) + ", " + typeName(ti.b) + "]";
      case Kind::Set: return "set[" + typeName(ti.a) + "]";
      case Kind::Heap: return "heap[" + typeName(ti.a) + "]";
      case Kind::MaxHeap: return "maxheap[" + typeName(ti.a) + "]";
      case Kind::Deque: return "deque[" + typeName(ti.a) + "]";
      case Kind::Struct: return ti.name;
    }
  }
  switch (t) {
    case Type::Int: return "int";
    case Type::Float: return "float";
    case Type::String: return "string";
    case Type::Bool: return "bool";
    case Type::Char: return "char";
    case Type::Void: return "void";
    case Type::EmptyArray: return "empty array";
    case Type::EmptyBrace: return "empty {}";
    default: return "<error>";
  }
}

}  // namespace ipx
