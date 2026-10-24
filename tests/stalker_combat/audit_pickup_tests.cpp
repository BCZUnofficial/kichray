
#include <vector>
#include "stalker_combat_pickup_policy.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using u32=unsigned int;
template<typename T>using xr_vector=std::vector<T>;
#define VERIFY(x) assert(x)
struct {u32 dwTimeGlobal;} Device;
struct Fvector {float x=0,y=0,z=0;};
struct Loc {u32 level_vertex_id()const{return 42;}};
struct CGameObject {unsigned id=1;unsigned ID()const{return id;}virtual ~CGameObject(){} Fvector pos; Loc loc;
 struct {int type=1;}spatial;
 int ef_weapon_type()const{return 6;} bool UsedAI_Locations(){return true;}
 bool getDestroy()const{return false;} CGameObject* H_Parent()const{return nullptr;}
 const Fvector& Position()const{return pos;} const Loc& ai_location()const{return loc;}
};
struct CInventory;
struct CInventoryItem: CGameObject {bool loaded=true;
 const CGameObject& object()const{return *this;} CGameObject& object(){return *this;}
 bool can_kill()const{return loaded;}
 CInventoryItem* can_kill(CInventory*)const{return loaded?const_cast<CInventoryItem*>(this):nullptr;}
 CInventoryItem* can_kill(const std::vector<const CGameObject*>&)const{return nullptr;}
 CInventoryItem* can_make_killing(CInventory*)const{return nullptr;}
 bool useful_for_NPC()const{return true;}
};
using TIItemContainer=std::vector<CInventoryItem*>;
struct CInventory {TIItemContainer m_all;};
template<typename T,typename U>T smart_cast(U* p){return dynamic_cast<T>(p);}
inline bool fsimilar(float a,float b){return std::fabs(a-b)<.00001f;}
struct Restriction {template<typename T>bool accessible(const T&)const{return true;}};
enum {eMentalStateDanger,eBodyStateCrouch,eBodyStateStand,eMovementTypeWalk,eMovementTypeStand,eObjectActionIdle};
namespace MovementManager {enum {ePathTypeLevelPath};}
namespace DetailPathManager {enum {eDetailPathTypeSmooth};}
struct Movement {Restriction rules;struct Path {bool value=false;bool failed()const{return value;}} path;
 Path& level_path(){return path;}
 void set_nearest_accessible_position(){++stops;}int stops=0;int requests=0;u32 target=0;Fvector destination;int type=0;
 Restriction& restrictions(){return rules;}
 void set_level_dest_vertex(u32 v){target=v;++requests;} void set_desired_position(const Fvector* p){destination=*p;}
 void set_desired_direction(const Fvector*){} void set_path_type(int){} void set_detail_path_type(int){}
 int body_state(){return eBodyStateStand;}void set_body_state(int){}void set_movement_type(int v){type=v;}
};
struct CAI_Stalker;
struct CObjectManager {bool is_useful(const CGameObject* p)const{return p->spatial.type==1;}};
struct CItemManager:CObjectManager {using inherited=CObjectManager;CAI_Stalker* m_object; std::vector<const CGameObject*> list;
 bool useful(const CGameObject*)const;
 const std::vector<const CGameObject*>& objects()const{return list;}
};
struct Memory {CItemManager items;struct Enemy {const CGameObject* selected(){static CGameObject enemy;return &enemy;}}enemies;
 CItemManager& item(){return items;}Enemy& enemy(){return enemies;}};
struct CAI_Stalker:CGameObject {bool m_item_actuality=false; CInventoryItem* m_best_item_to_kill=nullptr;
 const CInventoryItem *m_best_ammo=nullptr,*m_best_found_item_to_kill=nullptr,*m_best_found_ammo=nullptr;float m_best_item_value=0;
 CInventory bag;Memory memories;Movement moving;int goal=-1;
 CAI_Stalker(){memories.items.m_object=this;}CInventory& inventory(){return bag;}Memory& memory(){return memories;}Movement& movement(){return moving;}
 stalker_combat_pickup::Recovery m_combat_pickup;
 bool update_combat_pickup(const CInventoryItem*);
 void set_goal(int g){goal=g;}void update_best_item_info();bool item_to_kill();bool remember_item_to_kill();
};
struct EF {struct Non {CAI_Stalker* npc=nullptr;const CGameObject *target=nullptr,*itm=nullptr;
 CAI_Stalker*& member(){return npc;}const CGameObject*& enemy(){return target;}const CGameObject*& member_item(){return itm;}} state;
 struct Effect {float ffGetValue(){return 10.f;}}effect,*m_pfWeaponEffectiveness=&effect;
 void alife_evaluation(bool){}Non& non_alife(){return state;}};
struct AI {EF ef;struct Graph {bool valid_vertex_id(u32){return true;}}graph;
 EF& ef_storage(){return ef;}Graph* get_level_graph(){return &graph;}Graph& level_graph(){return graph;}} ai_state;
AI& ai(){return ai_state;}
struct Base {CAI_Stalker* npc;Base(CAI_Stalker* p):npc(p){}void execute(){}CAI_Stalker& object(){return *npc;}};
struct CStalkerActionGetItemToKill:Base {using inherited=Base;using Base::Base;void execute();};

#include "audit_pickup.generated.h"
int main(){
 for(bool path_failed:{false,true}){
    Device.dwTimeGlobal=0;CAI_Stalker npc;CInventoryItem gun;gun.pos.z=8;
    npc.memories.items.list.push_back(&gun);npc.moving.path.value=path_failed;
    CStalkerActionGetItemToKill action(&npc);
    for(;Device.dwTimeGlobal<=4000;Device.dwTimeGlobal+=100){
        assert(!npc.item_to_kill());if(npc.remember_item_to_kill())action.execute();
    }
    assert(!npc.remember_item_to_kill()&&npc.moving.stops==1);
    assert(npc.moving.requests<=(path_failed?3:30));
    CInventoryItem alternative;alternative.id=2;npc.memories.items.list.push_back(&alternative);
    assert(npc.remember_item_to_kill()&&npc.m_best_found_item_to_kill==&alternative);
    Device.dwTimeGlobal=14000;npc.memories.items.list.pop_back();
    assert(npc.remember_item_to_kill()&&npc.m_best_found_item_to_kill==&gun);
 }
 // Real movement refreshes progress but cannot defeat the overall travel deadline.
 stalker_combat_pickup::Recovery moving;
 for(unsigned t=0;t<15000;t+=100)assert(moving.approach(1,t,float(t)*.01f,0,0,false));
 assert(!moving.approach(1,15000,150,0,0,false));
 moving.forget(1);assert(!moving.rejected(1,15001));
 stalker_combat_pickup::Recovery wrapped;
 assert(wrapped.approach(1,0xffffff00u,0,0,0,false));
 assert(!wrapped.approach(1,0xffffff00u+3000u,0,0,0,false));
 assert(wrapped.rejected(1,0xffffff00u+3001u));
 std::puts("PASS: production pickup abandons failed/blocked routes, selects alternatives, retries after cooldown and handles clock wrap");
}
