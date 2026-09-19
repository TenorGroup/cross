#pragma once
#include <memory>
template<class T,class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args){return std::make_unique<T>(std::forward<Args>(args)...);}
