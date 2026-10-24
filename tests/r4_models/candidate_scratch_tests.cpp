#include "../../xr_3da/KichR4CandidateScratch.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <set>
#include <stdexcept>

static bool count_allocations=false;
static size_t allocations=0;
#ifdef _MSC_VER
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif
TEST_NOINLINE void* operator new(size_t n){if(count_allocations)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
TEST_NOINLINE void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}

namespace {
unsigned checks=0;
void Require(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
void Union(){
    std::vector<int> identities(1300);
    kich_r4_candidates::UniquePointers<int> seen;std::set<int*> baseline;
    std::vector<int*> expected,actual;uint32_t random=1234;
    for(unsigned frame=0;frame<2;++frame){
        seen.Begin();baseline.clear();expected.clear();actual.clear();
        for(unsigned view=0;view<65;++view)for(unsigned i=0;i<173;++i){
            random=random*1664525u+1013904223u;int* source=&identities[random%identities.size()];
            if(baseline.insert(source).second)expected.push_back(source);
            if(seen.Insert(source))actual.push_back(source);
        }
        Require(expected==actual,"camera/shadow source union preserves exact first-seen order and aliases");
        Require(seen.Size()==baseline.size(),"every unique source is admitted once per frame");
        Require(!seen.Insert(NULL),"null source has no identity");
    }
    const size_t capacity=seen.Capacity();allocations=0;count_allocations=true;
    seen.Begin();for(size_t i=0;i<identities.size();++i)seen.Insert(&identities[i]);
    for(size_t i=0;i<identities.size();++i)seen.Insert(&identities[i]);
    seen.Finish();
    count_allocations=false;
    Require(allocations==0 && seen.Capacity()==capacity,"warmed table has no per-frame/per-source allocations");
    seen.Begin();Require(seen.Size()==0 && seen.Insert(&identities[0]),"old source identities never suppress next-frame submissions");
}
void Budget(){
    std::vector<int> identities(65537);kich_r4_candidates::UniquePointers<int> seen;seen.Begin();
    for(size_t i=0;i<identities.size();++i)Require(seen.Insert(&identities[i]),"scratch budget cannot drop a source in a huge frame");
    Require(seen.Size()==identities.size() && seen.Capacity()>65536,"all sources survive before oversized scratch is trimmed");
    seen.Finish();Require(seen.Capacity()==0,"oversized frame storage is not retained");
    seen.Begin();Require(seen.Insert(&identities[0]),"small frame works after oversized scratch release");
}
}
int main(){try{Union();Budget();printf("E1.6 reusable native candidate identities: %u checks passed\n",checks);return 0;}
catch(const std::exception& e){count_allocations=false;fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
