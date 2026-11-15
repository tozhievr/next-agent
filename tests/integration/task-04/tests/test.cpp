#include "../interfaces/api.hpp"
#include <cassert>
#include <thread>
#include <vector>
#include <atomic>
int main(){BoundedQueue q(3);std::vector<int> out;std::thread producer([&]{for(int i=0;i<1000;i++)assert(q.push(i));q.close();});std::thread consumer([&]{int x;while(q.pop(x))out.push_back(x);});producer.join();consumer.join();assert(out.size()==1000);for(int i=0;i<1000;i++)assert(out[i]==i);assert(!q.push(2));int x;assert(!q.pop(x));}
