#ifndef KICH_R4_CANDIDATE_SCRATCH_H
#define KICH_R4_CANDIDATE_SCRATCH_H
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kich_r4_candidates {
// Source identities live only during Calculate. A flat table avoids allocating
// one tree node per renderable on every frame, without changing first-seen order.
template<class T> class UniquePointers {
    std::vector<T*> slots;
    size_t used;
    static size_t Hash(T* value){
        const uintptr_t address=reinterpret_cast<uintptr_t>(value);
        return static_cast<size_t>((address>>4)^(address>>13))*size_t(2654435761u);
    }
    size_t Find(T* value)const{
        size_t at=Hash(value)&(slots.size()-1);
        while(slots[at] && slots[at]!=value)at=(at+1)&(slots.size()-1);
        return at;
    }
public:
    UniquePointers():used(0){}
    void Begin(){std::fill(slots.begin(),slots.end(),static_cast<T*>(NULL));used=0;}
    bool Insert(T* value){
        if(!value)return false;
        if(!slots.empty() && slots[Find(value)])return false;
        if(slots.empty() || (used+1)*2>slots.size()){
            std::vector<T*> previous;previous.swap(slots);
            slots.assign(previous.empty()?64:previous.size()*2,static_cast<T*>(NULL));
            for(size_t i=0;i<previous.size();++i)if(previous[i])slots[Find(previous[i])]=previous[i];
        }
        slots[Find(value)]=value;++used;return true;
    }
    void Finish(){
        // This is a retained-scratch budget, never a source/draw quota. Even
        // an exceptional huge frame is processed completely before trimming.
        if(slots.size()>65536){std::vector<T*>().swap(slots);used=0;}
    }
    size_t Capacity()const{return slots.size();}
    size_t Size()const{return used;}
};
}
#endif
