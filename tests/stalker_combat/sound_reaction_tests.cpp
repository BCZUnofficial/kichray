#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#define MASTER_GOLD
#define SILENCE
#define SAVE_NON_ALIVE_OBJECT_SOUNDS
#define SAVE_FRIEND_ITEM_SOUNDS
#define SAVE_FRIEND_SOUNDS
#define IC inline
#define VERIFY(x) assert(x)
using u32=unsigned int;using u16=unsigned short;using squad_mask_type=unsigned int;
template<class T>using xr_vector=std::vector<T>;
template<class T> T _max(T a,T b){return std::max(a,b);}
bool _valid(float v){return std::isfinite(v);}
struct Fvector {float x=0,y=0,z=0;Fvector(){}Fvector(float a,float b,float c):x(a),y(b),z(c){}};
struct CObject{virtual ~CObject(){};u16 id=1;CObject* parent=nullptr;u16 ID()const{return id;}CObject* H_Parent()const{return parent;}};
struct CGameObject:CObject{Fvector current;bool hidden=false;const Fvector& Position()const{assert(!hidden);return current;}};
struct CEntityAlive:CGameObject{bool alive=true;int team=1;bool g_Alive()const{return alive;}int g_Team()const{return team;}};
template<class T,class U>T smart_cast(U* value){return dynamic_cast<T>(value);}
u16 object_id(const CObject* object){return object?object->ID():u16(-1);}
struct Memory {
    struct Enemy{const CEntityAlive* target=nullptr;const CEntityAlive* selected()const{return target;}}enemies;
    struct Hit{unsigned count=0;void add(CEntityAlive*){++count;}}hits;
    struct Visual{bool seen=false;bool visible_now(const CEntityAlive*)const{return seen;}}visuals;
    Enemy& enemy(){return enemies;}Hit& hit(){return hits;}Visual& visual(){return visuals;}
};
struct CCustomMonster:CEntityAlive {
    Memory memories;unsigned callbacks=0;
    void sound_callback(CObject*,int,const Fvector&,float){++callbacks;}
    Memory& memory(){return memories;}
};
struct CAI_Stalker:CCustomMonster {
    unsigned looks=0;Fvector heard;
    struct Agent {
        struct Members{u32 mask(CAI_Stalker*)const{return 1;}}members;
        Members& member(){return members;}
    } agent;
    Agent& agent_manager(){return agent;}
    void on_combat_heard_shot(const Fvector& p){++looks;heard=p;}
};
enum ESoundTypes {SOUND_TYPE_WEAPON=1,SOUND_TYPE_ITEM=2,SOUND_TYPE_MONSTER=4,
    SOUND_TYPE_ANOMALY=8,SOUND_TYPE_WORLD=16,SOUND_TYPE_WEAPON_SHOOTING=33};
struct CSoundObject {
    const CGameObject* m_object=nullptr;
    struct Params{Fvector m_position;u32 m_level_vertex_id=u32(-1);}m_object_params;
    struct Mask{u32 value=0;u32 get()const{return value;}}m_squad_mask;
    ESoundTypes type=SOUND_TYPE_WORLD;
    bool operator==(u16 id)const{return object_id(m_object)==id;}
    void fill(const CGameObject* source,const CGameObject*,ESoundTypes what,float,u32 mask){
        m_object=source;type=what;m_squad_mask.value=mask;
        // Production fill samples source transforms. A hidden source is forbidden in this fixture.
        m_object_params.m_position=source?source->Position():Fvector();
        m_object_params.m_level_vertex_id=source?999u:u32(-1);
    }
};
struct AI {
    struct Graph {bool valid=true,missing=false;bool valid_vertex_position(const Fvector&)const{return valid;}
        u32 vertex_id(const Fvector& p)const{return missing?u32(-1):u32(p.x*10);}
        bool valid_vertex_id(u32 id)const{return id!=u32(-1);}}graph;
    bool loaded=true;Graph* get_level_graph(){return loaded?&graph:nullptr;}Graph& level_graph(){return graph;}
} ai_fixture;
AI& ai(){return ai_fixture;}
struct {u32 dwTimeGlobal=10000;} Device;
struct UserData{void accept(int){}};using CSound_UserDataPtr=UserData*;
struct CSoundMemoryManager {
    std::vector<CSoundObject> sounds;std::vector<CSoundObject>* m_sounds=&sounds;
    CCustomMonster* m_object;CAI_Stalker* m_stalker;
    int m_visitor=0;float m_sound_threshold=.5f,m_weapon_factor=1,m_item_factor=1,m_npc_factor=1,m_anomaly_factor=1,m_world_factor=1;
    u32 m_last_sound_time=0;
    explicit CSoundMemoryManager(CCustomMonster& object,CAI_Stalker* stalker):m_object(&object),m_stalker(stalker){}
    void update_sound_threshold(){}
    void add(const CSoundObject& object){sounds.push_back(object);}
    void add(const CObject*,int,const Fvector&,float);
    void feel_sound_new(CObject*,int,CSound_UserDataPtr,const Fvector&,float);
};
#include "sound_reaction.generated.h"

int main(){
    CAI_Stalker npc; npc.id=1; CEntityAlive source;source.id=2;source.team=2;source.hidden=true;
    Fvector event(10,2,4);CSoundMemoryManager hearing(npc,&npc);
    hearing.feel_sound_new(&source,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,.49f);
    assert(npc.looks==0&&hearing.sounds.empty());
    hearing.feel_sound_new(&source,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,1.f);
    assert(npc.looks==1&&npc.heard.x==10&&npc.memories.hits.count==0);
    assert(!npc.memories.enemies.target); // Hearing doesn't fabricate a damage report/target.
    assert(hearing.sounds.size()==1&&hearing.sounds[0].m_object==&source);
    assert(hearing.sounds[0].m_object_params.m_position.x==10&&hearing.sounds[0].m_object_params.m_level_vertex_id==100);
    // Identity of a known enemy survives, but repeated shots store the new event sample, not live Position.
    npc.memories.enemies.target=&source;event=Fvector(12,2,3);Device.dwTimeGlobal+=100;
    hearing.feel_sound_new(&source,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,1.f);
    assert(npc.looks==2&&hearing.sounds.size()==1&&hearing.sounds[0].m_object_params.m_position.x==12);
    assert(hearing.sounds[0].m_object_params.m_level_vertex_id==120&&npc.memories.enemies.target==&source);
    // Own weapon shots must not produce a look; anonymous audible shots can, even during combat.
    CGameObject weapon;weapon.id=3;weapon.parent=&npc;
    hearing.feel_sound_new(&weapon,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,1.f);assert(npc.looks==2);
    hearing.feel_sound_new(&npc,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,1.f);assert(npc.looks==2);
    hearing.feel_sound_new(nullptr,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,1.f);assert(npc.looks==3);
    // Category scaling is applied before the actual audibility gate.
    CAI_Stalker quiet;CSoundMemoryManager muffled(quiet,&quiet);muffled.m_weapon_factor=.1f;
    muffled.feel_sound_new(&source,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,1.f);assert(quiet.looks==0);
    muffled.feel_sound_new(nullptr,SOUND_TYPE_WORLD,nullptr,event,1.f);assert(quiet.looks==0);
    // Off-mesh shots still turn the listener, but cannot erase its last usable enemy memory.
    event=Fvector(14,2,3);const unsigned looks=npc.looks;
    ai_fixture.graph.valid=false;
    hearing.feel_sound_new(&source,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,1.f);
    assert(npc.looks==looks+1&&npc.heard.x==14);
    assert(hearing.sounds[0].m_object_params.m_level_vertex_id==120);
    assert(hearing.sounds[0].m_object_params.m_position.x==12);
    ai_fixture.graph.valid=true;ai_fixture.graph.missing=true;
    // Inside graph bounds does not imply that this XZ cell contains a navigation node.
    hearing.feel_sound_new(&source,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,1.f);
    assert(npc.looks==looks+2&&hearing.sounds[0].m_object_params.m_level_vertex_id==120);
    assert(hearing.sounds[0].m_object_params.m_position.x==12);
    CAI_Stalker off_mesh;CSoundMemoryManager unmapped(off_mesh,&off_mesh);
    unmapped.feel_sound_new(&source,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,1.f);
    assert(off_mesh.looks==1&&unmapped.sounds.empty());
    ai_fixture.graph.missing=false;ai_fixture.loaded=false;
    unmapped.feel_sound_new(&source,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,1.f);
    assert(off_mesh.looks==2&&unmapped.sounds.empty());ai_fixture.loaded=true;
    // Generic monsters retain the previous hit-notification behavior.
    CCustomMonster monster;monster.id=4;CSoundMemoryManager old(monster,nullptr);source.hidden=false;
    old.feel_sound_new(&source,SOUND_TYPE_WEAPON_SHOOTING,nullptr,event,1.f);assert(monster.memories.hits.count==1);
    std::puts("PASS: production hearing threshold/scaling, shot event glance, no fake stalker hit, immutable sound position/nav snapshot, off-mesh memory protection, source identity and generic-monster behavior");
}
