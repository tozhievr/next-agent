#pragma once
#include <cstddef>
#include <optional>
class LRUCache { public: explicit LRUCache(std::size_t capacity); ~LRUCache(); LRUCache(const LRUCache&)=delete; LRUCache& operator=(const LRUCache&)=delete; void put(int key,int value); std::optional<int> get(int key); std::size_t size() const; private: struct Impl; Impl* impl_; };
