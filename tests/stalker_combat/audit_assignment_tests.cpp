
#include <vector>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cfloat>
#include <algorithm>
using u32=unsigned; using squad_mask_type=unsigned;
const float flt_max=FLT_MAX;
const bool TRUE=true;
bool fsimilar(float a,float b){return fabsf(a-b)<0.00001f;}
struct Mask{unsigned v=0; unsigned get()const{return v;} void set(unsigned n,bool){v|=n;}};
struct Entity{};
struct Member{float p=1; Entity e; float probability()const{return p;} void probability(float v){p=v;} Entity& object(){return e;}};
struct CAgentMemberManager{using iterator=std::vector<Member*>::iterator;std::vector<Member*> members;iterator member(unsigned mask){unsigned index=0;while((mask>>index)!=1u)++index;return members.begin()+index;}};
struct Agent{CAgentMemberManager mm; CAgentMemberManager& member(){return mm;}};
struct Enemy{Mask m_mask,m_distribute_mask;Entity* m_object;float m_probability=1;bool operator<(const Enemy&e)const{return m_probability<e.m_probability;}};
struct CAgentEnemyManager{using ENEMIES=std::vector<Enemy>; ENEMIES m_enemies; Agent a; float value; unsigned calls=0; Agent& object(){return a;} float evaluate(Entity*,Entity*){++calls;return value;} void assign_enemies();};

#include "audit_assignment.generated.h"
int main(){
 for(float value:{0.f,.5f,1.f,.999999f}) {
    CAgentEnemyManager x;Member a,b,c;Entity enemy;
    x.value=value;x.a.mm.members={&a,&b,&c};
    Enemy e;e.m_object=&enemy;e.m_mask.v=7;x.m_enemies.push_back(e);x.m_enemies.push_back(e);
    x.assign_enemies();
    unsigned assigned=0;
    for(const auto& target:x.m_enemies){assert(!(assigned&target.m_distribute_mask.v));assigned|=target.m_distribute_mask.v;}
    assert(assigned==7 && x.calls<=12);
 }
 CAgentEnemyManager empty;empty.assign_enemies();assert(!empty.calls);
 std::puts("PASS: enemy assignment terminates at exact/near-one probability and assigns each member once");
}
