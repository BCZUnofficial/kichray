#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cfloat>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "stalker_combat_memory_utils.h"

#define USE_LEVEL_TIME
#define USE_LAST_LEVEL_TIME
#define MASTER_GOLD
#define VERIFY(condition) do { if (!(condition)) throw std::runtime_error("VERIFY: " #condition); } while (0)
#define IC
#define TRUE true
typedef uint32_t u32;
typedef uint16_t u16;
const float flt_max=FLT_MAX;
template<class T> using xr_vector = std::vector<T>;
template<class T> T smart_cast(const class CObject* object) { return dynamic_cast<T>(object); }
inline float _sqr(float value) { return value * value; }
inline bool fis_zero(float value) { return std::fabs(value) < 1.e-6f; }

struct Clock { u32 dwTimeGlobal; } Device;
struct Fvector {
    float x, y, z;
    Fvector(float px=0, float py=0, float pz=0) : x(px), y(py), z(pz) {}
    float distance_to_sqr(const Fvector& other) const {
        return _sqr(x-other.x) + _sqr(y-other.y) + _sqr(z-other.z);
    }
};
struct Flags {
    u32 value;
    Flags() : value(0) {}
    void zero() { value=0; }
    void assign(u32 bits) { value=bits; }
    u32 get() const { return value; }
    void set(u32 bits, bool enabled) { value = enabled ? value|bits : value&~bits; }
};
class CObject {
public:
    u16 id;
    bool alive, destroyed, enemy, visible, position_forbidden;
    unsigned CLS_ID;
    Fvector position;
    float score;
    explicit CObject(u16 identifier=0) : id(identifier), alive(true), destroyed(false), enemy(true),
        visible(false), position_forbidden(false), CLS_ID(0), position(), score(9000.f) {}
    virtual ~CObject() {}
    u16 ID() const { return id; }
    bool g_Alive() const { return alive; }
    bool getDestroy() const { return destroyed; }
    const Fvector& Position() const {
        if (position_forbidden) throw std::runtime_error("hidden target Position() was queried");
        return position;
    }
};
typedef CObject CGameObject;
class CEntityAlive : public CObject {
public:
    explicit CEntityAlive(u16 id=0) : CObject(id) {}
};
struct Params {
    Fvector m_position;
    u32 m_level_vertex_id;
    Params() : m_position(), m_level_vertex_id(0) {}
    void fill(const CObject* object) { m_position=object->Position(); }
};
struct Record {
    u32 m_level_time, m_last_level_time, m_first_level_time;
    Record() : m_level_time(0), m_last_level_time(0), m_first_level_time(0) {}
};
struct CVisibleObject : Record {
    const CGameObject* m_object;
    bool m_enabled;
    Flags m_visible, m_squad_mask;
    Params m_object_params, m_self_params;
    CVisibleObject() : m_object(0), m_enabled(true) {}
    bool visible(u32 mask) const { return (m_visible.get()&mask)!=0; }
    void visible(u32 mask, bool value) { m_visible.set(mask,value); }
    void fill(const CGameObject* target, const CGameObject* self, u32 squad, u32 visible) {
        m_object=target; m_object_params.fill(target); m_self_params.fill(self);
        m_squad_mask.assign(squad); m_visible.assign(visible); m_enabled=true;
        m_level_time=Device.dwTimeGlobal;
    }
};
struct ObjectId { u16 id; };
inline ObjectId object_id(const CObject* object) { ObjectId id={object->ID()}; return id; }
inline bool operator==(const CVisibleObject& object, ObjectId id) { return object.m_object && object.m_object->ID()==id.id; }
template<class T> struct SLevelTimePredicate {
    bool operator()(const CVisibleObject& a, const CVisibleObject& b) const { return a.m_level_time<b.m_level_time; }
};
namespace MemorySpace {
struct CMemoryInfo : CVisibleObject {
    bool m_visual_info, m_sound_info, m_hit_info;
    CMemoryInfo() : m_visual_info(false), m_sound_info(false), m_hit_info(false) {}
};
}
struct CNotYetVisibleObject { float m_value; u32 m_update_time; };
struct CVisionParameters { float m_visibility_threshold; u32 m_still_visible_time; };
struct DummyRestrictions {};
struct DummyMovement {
    DummyRestrictions data;
    DummyRestrictions& restrictions() { return data; }
};
struct DummyEnemyAssignments {
    bool assigned_wounded(const CObject*, const CObject*) const { return false; }
};
struct DummyAgentManager {
    DummyEnemyAssignments data;
    DummyEnemyAssignments& enemy() { return data; }
};
class CMemoryManager;
class CCustomMonster : public CEntityAlive {
public:
    CMemoryManager* senses;
    unsigned changes;
    DummyMovement move;
    explicit CCustomMonster(u16 id=0) : CEntityAlive(id), senses(0), changes(0) {}
    CMemoryManager& memory() const { return *senses; }
    bool is_relation_enemy(const CEntityAlive* target) const { return target->enemy; }
    void on_enemy_change(const CEntityAlive*) { ++changes; }
    DummyMovement& movement() { return move; }
};
class CAI_Stalker : public CCustomMonster {
public:
    bool is_wounded;
    DummyAgentManager agents;
    explicit CAI_Stalker(u16 id=0) : CCustomMonster(id), is_wounded(false) {}
    bool wounded() const { return is_wounded; }
    bool wounded(const DummyRestrictions*) const { return is_wounded; }
    DummyAgentManager& agent_manager() { return agents; }
};
class CHitMemoryManager {
public:
    bool m_stalker;
    u16 m_last_hit_object_id, m_recent_hit_object_id;
    u32 m_recent_hit_time, m_recent_hit_revision;
    CHitMemoryManager() : m_stalker(true), m_last_hit_object_id(u16(-1)), m_recent_hit_object_id(u16(-1)),
        m_recent_hit_time(0), m_recent_hit_revision(0) {}
    u32 recent_hit_revision() const { return m_recent_hit_revision; }
    const u16& last_hit_object_id() const { return m_last_hit_object_id; }
    float recent_hit_weight(const CEntityAlive*) const;
    void record_test_hit(const CObject*, float);
};
class CVisualMemoryManager {
public:
    typedef u32 squad_mask_type;
    typedef std::vector<CVisibleObject> VISIBLES;
    struct CDirectSight { const CObject* m_object; u32 m_time; bool m_current_update; };
    typedef std::vector<CDirectSight> DIRECT_SIGHTS;
    DIRECT_SIGHTS m_direct_sights;
    bool m_stalker;
    bool m_enabled;
    CCustomMonster* m_object;
    std::vector<CVisibleObject> records;
    std::vector<CVisibleObject>* m_objects;
    u32 m_max_object_count;
    u32 m_last_update_time;
    u32 recipient_mask;
    std::vector<CNotYetVisibleObject> m_not_yet_visible_objects;
    CVisionParameters params;
    CVisualMemoryManager() : m_stalker(true), m_enabled(true), m_object(0), m_objects(&records), m_max_object_count(32),
        m_last_update_time(u32(-1)), recipient_mask(1) {
        params.m_visibility_threshold=120.f;
        params.m_still_visible_time=0;
    }
    u32 mask() const { return recipient_mask; }
    bool enabled() const;
    void enable(bool);
    const VISIBLES& objects() const { return *m_objects; }
    bool visible_right_now(const CGameObject*) const;
    bool visible_now(const CGameObject*) const;
    bool visible(const CGameObject* object, float) const { return object && object->visible; }
    CVisibleObject* visible_object(const CGameObject* target) {
        for (size_t i=0; i<m_objects->size(); ++i) if ((*m_objects)[i].m_object==target) return &(*m_objects)[i];
        return 0;
    }
    void add_visible_object(const CVisibleObject);
    void add_visible_object(const CObject*, float, bool = false);
    u32 direct_visible_time(const CObject*) const;
    bool direct_visible_in_update(const CObject*) const;
    void reset_direct_sight_update();
    void remember_direct_sight(const CObject*);
    void forget_direct_sight(const CObject*);
    const CVisionParameters& current_state() const { return params; }
    bool add_shared_memory(const CVisibleObject&);
    void begin_test_update();
    void decay_test_update(float);
};
class CMemoryManager {
public:
    typedef MemorySpace::CMemoryInfo CMemoryInfo;
    bool m_stalker;
    CCustomMonster* m_object;
    CVisualMemoryManager visual_manager;
    CHitMemoryManager hit_manager;
    explicit CMemoryManager(CCustomMonster* owner) : m_stalker(true), m_object(owner) {
        owner->senses=this; visual_manager.m_object=owner;
    }
    CVisualMemoryManager& visual() const { return const_cast<CVisualMemoryManager&>(visual_manager); }
    CHitMemoryManager& hit() const { return const_cast<CHitMemoryManager&>(hit_manager); }
    CMemoryInfo memory(const CObject* object) const {
        CMemoryInfo info;
        const CVisibleObject* visible=visual().visible_object(object);
        if (visible) {
            static_cast<CVisibleObject&>(info)=*visible;
            info.m_visual_info=true;
        }
        return info;
    }
    bool share_enemy_memory(const CMemoryInfo&, u16);
};
struct DummyNonAlife {
    const CObject* member_ptr;
    const CObject* enemy_ptr;
    void* member_item_ptr;
    void* enemy_item_ptr;
    DummyNonAlife() : member_ptr(0),enemy_ptr(0),member_item_ptr(0),enemy_item_ptr(0) {}
    const CObject*& member() { return member_ptr; }
    const CObject*& enemy() { return enemy_ptr; }
    void*& member_item() { return member_item_ptr; }
    void*& enemy_item() { return enemy_item_ptr; }
};
struct DummyVictoryProbability { float ffGetValue() const { return 0.f; } };
struct DummyEfStorage {
    DummyNonAlife state;
    DummyVictoryProbability probability;
    DummyVictoryProbability* m_pfVictoryProbability;
    DummyEfStorage() : m_pfVictoryProbability(&probability) {}
    DummyNonAlife& non_alife() { return state; }
};
struct DummyAI {
    DummyEfStorage storage;
    DummyEfStorage& ef_storage() { return storage; }
};
inline DummyAI& ai() { static DummyAI instance; return instance; }
namespace stalker_combat_diagnostics {
enum Event { target, memory, cover, movement, fire, grenade, action };
inline bool enabled(unsigned int) { return false; }
inline void log(unsigned int, Event, const char*, const char*, ...) {}
}
const unsigned CLSID_OBJECT_ACTOR=1;
class ObjectSelector {
public:
    typedef std::vector<const CEntityAlive*> OBJECTS;
    typedef OBJECTS ENEMIES;
    OBJECTS m_objects;
    const CEntityAlive* m_selected;
    unsigned evaluations;
    ObjectSelector() : m_selected(0), evaluations(0) {}
    float do_evaluate(const CEntityAlive* object) { ++evaluations; return object->score; }
    void update() {
        float best=1.e30f; m_selected=0;
        for(size_t i=0;i<m_objects.size();++i) {
            const float score=do_evaluate(m_objects[i]);
            if (score<best) {best=score;m_selected=m_objects[i];}
        }
    }
    const CEntityAlive* selected() const { return m_selected; }
};
class CEnemyManager : public ObjectSelector {
public:
    typedef ObjectSelector inherited;
    CCustomMonster* m_object;
    CAI_Stalker* m_stalker;
    bool allow_change;
    mutable bool m_ready_to_save;
    u32 m_last_enemy_change, m_last_threat_check, m_last_threat_hit_revision;
    explicit CEnemyManager(CAI_Stalker* owner) : m_object(owner),m_stalker(owner),allow_change(true),m_ready_to_save(true),
        m_last_enemy_change(0),m_last_threat_check(0),m_last_threat_hit_revision(0) {}
    bool enable_enemy_change() const { return allow_change; }
    float evaluate(const CEntityAlive*) const;
    bool change_from_wounded(const CEntityAlive*,const CEntityAlive*) const;
    bool enemy_inertia(const CEntityAlive*) const;
    void on_enemy_change(const CEntityAlive*);
    void remove_wounded();
    void process_wounded(bool&);
    bool need_update(const bool&) const;
    void try_change_stalker_enemy(const CEntityAlive*,const bool&);
    void try_change_enemy();
};
struct Packet {
    std::vector<unsigned char> bytes;
    size_t cursor;
    Packet() : cursor(0) {}
    void w_u32(u32 value) { for(unsigned i=0;i<4;++i) bytes.push_back(static_cast<unsigned char>(value>>(8*i))); }
    u32 r_u32() {
        VERIFY(cursor+4<=bytes.size());u32 value=0;
        for(unsigned i=0;i<4;++i) value|=u32(bytes[cursor++])<<(8*i);
        return value;
    }
};
