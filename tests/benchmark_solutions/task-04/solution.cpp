#include "interfaces/api.hpp"
#include <mutex>
#include <condition_variable>
#include <deque>
#include <stdexcept>
struct BoundedQueue::Impl{std::size_t cap;bool closed=false;std::mutex mutex;std::condition_variable cv;std::deque<int> q;explicit Impl(std::size_t c):cap(c){if(!c)throw std::invalid_argument("zero capacity");}};
BoundedQueue::BoundedQueue(std::size_t c):impl_(new Impl(c)){} BoundedQueue::~BoundedQueue(){close();delete impl_;}
bool BoundedQueue::push(int v){std::unique_lock lock(impl_->mutex);impl_->cv.wait(lock,[&]{return impl_->closed||impl_->q.size()<impl_->cap;});if(impl_->closed)return false;impl_->q.push_back(v);impl_->cv.notify_all();return true;}
bool BoundedQueue::pop(int& v){std::unique_lock lock(impl_->mutex);impl_->cv.wait(lock,[&]{return impl_->closed||!impl_->q.empty();});if(impl_->q.empty())return false;v=impl_->q.front();impl_->q.pop_front();impl_->cv.notify_all();return true;}
void BoundedQueue::close(){std::lock_guard lock(impl_->mutex);impl_->closed=true;impl_->cv.notify_all();}
