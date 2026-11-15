#pragma once
#include <cstddef>
class BoundedQueue { public: explicit BoundedQueue(std::size_t capacity); ~BoundedQueue(); BoundedQueue(const BoundedQueue&)=delete; BoundedQueue& operator=(const BoundedQueue&)=delete; bool push(int value); bool pop(int& value); void close(); private: struct Impl; Impl* impl_; };
