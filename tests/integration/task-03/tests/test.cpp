#include "../interfaces/api.hpp"
#include <cassert>
int main(){LRUCache c(2);c.put(1,10);c.put(2,20);assert(c.get(1)==10);c.put(3,30);assert(!c.get(2));assert(c.get(1)==10);assert(c.get(3)==30);c.put(1,11);assert(c.get(1)==11);assert(c.size()==2);LRUCache d(1);d.put(9,1);d.put(8,2);assert(!d.get(9));}
