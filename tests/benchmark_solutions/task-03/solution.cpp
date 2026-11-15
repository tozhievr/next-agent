#include "interfaces/api.hpp"
#include <list>
#include <unordered_map>
struct LRUCache::Impl {std::size_t cap;std::list<std::pair<int,int>> values;std::unordered_map<int,decltype(values)::iterator> index;};
LRUCache::LRUCache(std::size_t c):impl_(new Impl{c,{},{}}){} LRUCache::~LRUCache(){delete impl_;}
void LRUCache::put(int k,int v){if(!impl_->cap)return;auto it=impl_->index.find(k);if(it!=impl_->index.end()){it->second->second=v;impl_->values.splice(impl_->values.begin(),impl_->values,it->second);return;}impl_->values.emplace_front(k,v);impl_->index[k]=impl_->values.begin();if(impl_->index.size()>impl_->cap){impl_->index.erase(impl_->values.back().first);impl_->values.pop_back();}}
std::optional<int> LRUCache::get(int k){auto it=impl_->index.find(k);if(it==impl_->index.end())return {};impl_->values.splice(impl_->values.begin(),impl_->values,it->second);return it->second->second;}
std::size_t LRUCache::size()const{return impl_->index.size();}
