#pragma once
#include <memory>
#include <new>
template <class T>
std::unique_ptr<T> makeUniqueNoThrow() {
  return std::unique_ptr<T>(new (std::nothrow) T());
}
