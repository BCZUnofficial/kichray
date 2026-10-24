#pragma once
#include <cassert>
#define VERIFY(x) assert(x)
const float flt_max=3.402823466e38f;
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include "stalker_combat_awareness_policy.h"
#include "stalker_combat_cover_policy.h"
using u32=unsigned int; using u16=std::uint16_t; using LPCSTR=const char*; using squad_mask_type=unsigned int;
template<class T> using xr_vector=std::vector<T>;
template<class T,class U> T smart_cast(U* p) {return dynamic_cast<T>(p);}
const float EPS_L=.0001f;
float _sqrt(float v){return std::sqrt(v);}
bool _valid(float v){return std::isfinite(v);}
bool fis_zero(float v){return std::fabs(v)<EPS_L;}
struct Fvector {
 float x=0,y=0,z=0;
 Fvector(){} Fvector(float a,float b,float c):x(a),y(b),z(c){}
 Fvector& set(float a,float b,float c){x=a;y=b;z=c;return *this;}
 Fvector& sub(const Fvector& a,const Fvector& b){return set(a.x-b.x,a.y-b.y,a.z-b.z);}
 Fvector& mad(const Fvector& a,const Fvector& b,float s){return set(a.x+b.x*s,a.y+b.y*s,a.z+b.z*s);}
 Fvector& invert(){x=-x;y=-y;z=-z;return *this;}
 float square_magnitude() const {return x*x+y*y+z*z;}
 float magnitude() const {return std::sqrt(square_magnitude());}
 Fvector& div(float n){x/=n;y/=n;z/=n;return *this;}
 Fvector& normalize_safe(){float n=magnitude();if(n>EPS_L)div(n);return *this;}
 float dotproduct(const Fvector& p) const {return x*p.x+y*p.y+z*p.z;}
 float distance_to_sqr(const Fvector& p) const {return (x-p.x)*(x-p.x)+(y-p.y)*(y-p.y)+(z-p.z)*(z-p.z);}
 float distance_to_xz_sqr(const Fvector& p) const {return (x-p.x)*(x-p.x)+(z-p.z)*(z-p.z);}
 float distance_to(const Fvector& p) const {return std::sqrt(distance_to_sqr(p));}
 void getHP(float& h,float& p) const {h=std::atan2(x,z);p=std::atan2(y,std::sqrt(x*x+z*z));}
};
struct DeviceData {u32 dwTimeGlobal=10000;} Device;
struct CObject {
 struct SavedPosition {u32 dwTime;Fvector vPosition;};
 virtual ~CObject(){} u16 id=1;Fvector pos;bool hidden=false;
 std::vector<SavedPosition> samples;
 u16 ID() const {return id;}
 const Fvector& Position() const {assert(!hidden);return pos;}
 void Center(Fvector& p) const {p=Position();p.y+=1;}
 u32 ps_Size() const{return u32(samples.size());}
 SavedPosition ps_Element(u32 n) const{return samples.at(n);}
 bool use_center_to_aim() const{return false;}
};
using CGameObject=CObject;
struct CEntityAlive:CObject {bool alive=true;bool g_Alive() const{return alive;}};
struct CDangerObject {enum Type {eDangerTypeGrenade,eDangerTypeOther};Type value=eDangerTypeGrenade;Type type() const{return value;}};
struct CVisibleObject {bool squad=false;bool visible(u32) const{return squad;}};
struct CMemoryInfo {const CEntityAlive* m_object=nullptr;u32 m_level_time=10000;
 struct Params {Fvector m_position;u32 m_level_vertex_id=2;} m_object_params,m_self_params;};
struct Memory {
 struct Enemies {const CEntityAlive* target=nullptr;const CEntityAlive* selected() const{return target;}} enemies;
 struct Visual {bool seen=false;CVisibleObject record;bool visible_now(const CEntityAlive*) const{return seen;}
 const CVisibleObject* visible_object(const CEntityAlive*) const{return &record;}} visuals;
 struct Danger {CDangerObject* danger=nullptr;const CDangerObject* selected() const{return danger;}} dangers;
 struct Hit {u32 revision=0;u32 recent_hit_revision() const{return revision;}} hits;
 CMemoryInfo info;bool disabled=false;
 Enemies& enemy(){return enemies;} Visual& visual(){return visuals;} Danger& danger(){return dangers;} Hit& hit(){return hits;}
 CMemoryInfo memory(const CEntityAlive*) const {return info;}
 void enable(const CEntityAlive*,bool value){disabled=!value;}
};
enum {eMovementTypeStand,eMovementTypeWalk,eMovementTypeRun,eBodyStateStand,eBodyStateCrouch,eMentalStateDanger};
namespace MovementManager {enum {ePathTypeLevelPath};} namespace DetailPathManager {enum {eDetailPathTypeSmooth};}
namespace SightManager {enum {eSightTypePosition};}
struct CSightAction {Fvector point;CSightAction(int,const Fvector& p,bool):point(p){}};
struct Movement {
 struct Level {bool fail=false;bool failed() const{return fail;}} level;
 bool complete=false,access=true;int body=eBodyStateStand,type=eMovementTypeStand;Fvector desired;u32 dest=0,changes=0;
 void set_desired_position(const Fvector* p){if(p)desired=*p;}
 void set_desired_direction(const Fvector*){} void set_path_type(int){} void set_detail_path_type(int){} void set_mental_state(int){}
 int target_body_state() const{return body;} void set_body_state(int v){body=v;} void set_movement_type(int v){type=v;}
 bool accessible(u32) const{return access;} bool accessible(const Fvector&) const{return access;}
 void set_level_dest_vertex(u32 v){dest=v;complete=false;++changes;}
 void set_nearest_accessible_position(){desired=Fvector();complete=true;}
 void set_nearest_accessible_position(const Fvector& p,u32 v){desired=p;dest=v;complete=false;}
 bool path_completed() const{return complete;} Level& level_path(){return level;}
};
struct CAI_Stalker;
struct CAgentMemberManager {
 stalker_combat_awareness::ScoutLease m_scout;bool grouped=false,registered=true;
 bool claim_scout(const CAI_Stalker*,u16,u32);void release_scout(u16);bool other_scout(u16,u16,u32) const;
 bool registered_in_combat(const CAI_Stalker*) const{return registered;}
 bool group_behaviour() const{return grouped;}
 struct Order {void cover(int){}} order;Order& member(CAI_Stalker*){return order;}
};
struct Agent {CAgentMemberManager* manager=nullptr;CAgentMemberManager& member(){return *manager;}};
struct CAI_Stalker:CEntityAlive {
 Memory memories;Movement moving;Agent agent;
 struct Sight {Fvector last;void setup(const CSightAction& a){last=a.point;}} sights;
 struct Condition {float hp=1;float health() const{return hp;}} condition;
 struct Animation {
 std::vector<int> scripts;bool available=false,active=false;u32 starts=0,stops=0;
 const std::vector<int>& script_animations()const{return scripts;}
 bool start_search_listen(){++starts;active=available;return active;}
 bool search_listen_active() const{return active;}
 void stop_search_listen(){++stops;active=false;}
 } animations;
 struct Matrix {Fvector c=Fvector(0,1.5f,0),k=Fvector(0,0,1);void transform_tiny(Fvector& p,const Fvector& v)const{p=v;}} eye_matrix;
 stalker_combat_awareness::Peripheral m_combat_peripheral;Fvector m_combat_peripheral_point;
 bool script=false,injured=false,critical=false,talking=false,relation=true;
 u32 near_miss_revision=0;bool near_miss_recent=false;
 u32 combat_near_miss_revision() const{return near_miss_revision;}
 bool combat_near_miss_recent(u32) const{return near_miss_recent;}
 Memory& memory(){assert(alive);return memories;} Movement& movement(){return moving;} Agent& agent_manager(){assert(alive);return agent;}
 Sight& sight(){return sights;} Condition& conditions(){return condition;} Animation& animation(){return animations;}
 bool wounded()const{return injured;}bool critically_wounded()const{return critical;}bool GetScriptControl()const{return script;}bool IsTalking()const{return talking;}
 bool is_relation_enemy(CEntityAlive*)const{return relation;}
 bool combat_target_position(Fvector& p,bool) {if(!memories.info.m_object)return false;p=memories.info.m_object_params.m_position;return true;}
 void update_combat_peripheral();bool combat_peripheral_look(Fvector& p);
 const Matrix& XFORM() const{return eye_matrix;}
};
struct CSightManager {CAI_Stalker* m_object;void SetFirePointLookAngles(const Fvector&,float&,float&,const CGameObject* object=nullptr);};
namespace collide {enum {rqtBoth};struct rq_result {CObject* O=nullptr;};}
struct ObjectSpace {
 std::vector<CObject*> nearby;bool blocked=false;u32 rays=0,queries=0;
 void GetNearest(std::vector<CObject*>& out,const Fvector&,float,CObject*){out=nearby;++queries;}
 bool RayPick(const Fvector&,const Fvector&,float,int,collide::rq_result& hit,CObject*){++rays;hit.O=nullptr;return blocked;}
};
struct LevelData {struct ObjectSpace ObjectSpace;} level;
LevelData& Level(){return level;}
namespace stalker_combat_diagnostics {
 enum Event{action};std::vector<std::string> events;
 bool enabled(u32){return true;}
 void log(u32,Event,const char* reason,const char*,...){events.push_back(reason);}
}
struct CStalkerActionCombatBase {
 CAI_Stalker* m_object;
 CStalkerActionCombatBase(CAI_Stalker* o,LPCSTR):m_object(o){}
 virtual ~CStalkerActionCombatBase(){}
 virtual void initialize(){}virtual void execute(){}virtual void finalize(){}
 CAI_Stalker& object()const{return *m_object;}void aim_ready(){}
};
bool route=true,space=true;u32 route_queries=0;
bool stalker_local_destination(CAI_Stalker&,const Fvector& requested,Fvector& dest,u32& v){++route_queries;dest=requested;v=3;return route;}
bool stalker_local_space(CAI_Stalker&,const Fvector&,u32,bool){return space;}
#include "awareness.generated.h"
