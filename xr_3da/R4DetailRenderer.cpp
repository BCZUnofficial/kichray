#include "KichR4NativePrelude.h"
#include "KichR4DetailData.h"
#include "KichR4Native.h"
#include "KichR4FrameMetrics.h"
#include "KichR4QueueProfile.h"
#include "RenderSnapshot.h"
#include "IGame_Persistent.h"
#include "Environment.h"
#include <functional>
extern float r_ssaDISCARD;

namespace {
struct ShadowDetailItem {const CDetailManager::SlotItem* item;float scale;uint32_t cell;};
KichR4DetailInstance DetailInstance(const CDetailManager::SlotItem& item,float scale,unsigned variant){
    const Fmatrix& m=item.mRotY;KichR4DetailInstance instance={};
    const float rows[3][4]={{m._11*scale,m._21*scale,m._31*scale,m._41},
        {m._12*scale,m._22*scale,m._32*scale,m._42},{m._13*scale,m._23*scale,m._33*scale,m._43}};
    memcpy(instance.row,rows,sizeof(rows));instance.lighting[0]=item.c_sun;instance.lighting[1]=item.c_hemi;
    instance.lighting[2]=static_cast<float>(variant);return instance;
}
}

kich_r4_detail::Frame& KichR4_DetailFrame(){static kich_r4_detail::Frame value;return value;}
void KichR4_DetailUnload(){KichR4_DetailFrame().Reset();}
void KichR4_DetailLoad(CDetailManager& manager){
    using namespace kich_r4_detail;
    Frame& frame=KichR4_DetailFrame();frame.Reset();
    for(size_t i=0;i<manager.objects.size();++i){
        CDetail& source=*manager.objects[i];Mesh mesh;
        if(!source.number_vertices || source.number_vertices>65536 || !source.number_indices || source.number_indices%3)
            throw std::runtime_error("invalid level.details mesh counts");
        mesh.vertices.resize(source.number_vertices);mesh.indices.assign(source.indices,source.indices+source.number_indices);
        for(size_t j=0;j<mesh.indices.size();++j)if(mesh.indices[j]>=source.number_vertices)throw std::runtime_error("level.details index out of range");
        const float height=source.bv_bb.max.y-source.bv_bb.min.y;
        for(uint32_t j=0;j<source.number_vertices;++j){
            const CDetail::fvfVertexIn& v=source.vertices[j];KichR4DetailVertex& out=mesh.vertices[j];
            out.position[0]=v.P.x;out.position[1]=v.P.y;out.position[2]=v.P.z;
            for(unsigned k=0;k<3;++k)if(!Finite(out.position[k]))throw std::runtime_error("invalid detail position");
            out.misc[0]=Quantized(v.u);out.misc[1]=Quantized(v.v);
            out.misc[2]=height>1.e-6f?Quantized(v.P.y/height):0;out.misc[3]=0;
        }
        if(source.shader && source.shader->E[0] && !source.shader->E[0]->passes.empty()){
            SPass& pass=*source.shader->E[0]->passes[0];
            if(pass.vs)mesh.shader=pass.vs->cName.c_str();
            if(pass.T)for(STextureList::iterator t=pass.T->begin();t!=pass.T->end();++t)
                if(t->first==0 && t->second){mesh.texture=t->second->cName.c_str();break;}
        }
        char name[48];sprintf_s(name,"level.details/mesh_%u",static_cast<unsigned>(i));mesh.name=name;
        mesh.CalculateBounds();frame.meshes.push_back(std::move(mesh));
    }
    Msg("[R4 G3a] details loaded: meshes=%u; native DX11 instancing; legacy detail buffers/capture=0",static_cast<u32>(frame.meshes.size()));
    for(size_t i=0;i<frame.meshes.size();++i){const Mesh& m=frame.meshes[i];
        Msg("[R4 P0-S4] %s height=%.3f m sun_caster=%u",m.name.c_str(),m.bounds_known?m.maximum[1]-m.minimum[1]:-1.f,FlatGroundClutter(m)?0u:1u);}
}
namespace {
// Fills frame's per-frame arrays from the manager's visibility lists of this
// frame (consumed) and its cache. meshes: the loaded, immutable mesh table.
// The scratch below is shared by its two callers, which never overlap.
void PrepareInto(CDetailManager& manager,const std::vector<kich_r4_detail::Mesh>& meshes,kich_r4_detail::Frame& frame,const CDetailManager::SSwingValue& swing){
    using namespace kich_r4_detail;
    const auto start=kich_r4_metrics::Start(g_render_snapshot_active);frame.ClearFrame(Device.dwFrame);
    // UpdateVisibleM caches scale and wind/still admission for 15-30 frames.
    // Keep the same pose for camera-visible items in every shadow pass; a
    // second distance calculation would make their shadows shrink/stop early.
    // Index cache-part lists, not individual instances, and retain scratch space.
    static std::vector<const CDetailManager::SlotItemVec*> camera_parts;
    camera_parts.clear();
    if(!Finite(swing.rot1) || !Finite(swing.rot2) || swing.rot1<=0 || swing.rot2<=0 || !Finite(swing.speed) || !Finite(swing.amp1) || !Finite(swing.amp2))
        throw std::runtime_error("invalid detail swing settings");
    /* Preserve KichRay R2's absolute-time phase, not CoP's accumulated timer. */
    const float rotations[2]={PI_MUL_2*Device.fTimeGlobal/swing.rot1,PI_MUL_2*Device.fTimeGlobal/swing.rot2};
    const float amplitudes[2]={swing.amp1,swing.amp2};
    for(unsigned variant=0;variant<3;++variant){
        if(variant){const unsigned n=variant-1;
            frame.wave[variant][0]=(variant==1?1.f/5:1.f/3)/PI_MUL_2;
            frame.wave[variant][1]=(1.f/7)/PI_MUL_2;frame.wave[variant][2]=(variant==1?1.f/3:1.f/5)/PI_MUL_2;
            frame.wave[variant][3]=Device.fTimeGlobal*swing.speed/PI_MUL_2;
            frame.wind[variant][0]=sinf(rotations[n])*amplitudes[n];frame.wind[variant][2]=cosf(rotations[n])*amplitudes[n];}
        for(size_t mesh=0;mesh<manager.objects.size();++mesh){
            CDetailManager::vis_list& list=manager.m_visibles[variant];
            kich_r4_detail::Group group={static_cast<uint32_t>(mesh),variant,static_cast<uint32_t>(frame.instances.size()),0};
            for(size_t slot=0;slot<list[mesh].size();++slot){CDetailManager::SlotItemVec& items=*list[mesh][slot];
                if(!items.empty())camera_parts.push_back(&items);
                for(size_t i=0;i<items.size();++i){const CDetailManager::SlotItem& item=*items[i];
                    frame.Append(DetailInstance(item,item.scale_calculated,variant));++group.count;++frame.variants[variant];}
            }
            list[mesh].clear_not_free();if(group.count)frame.groups.push_back(group);
        }
    }
    /* X-Ray's detail cache owns original item transforms independently of the
     * camera-visible r_items. Gather ready cells without a camera-frustum test;
     * each shadow pass will subsequently cull these compact instances itself.
     * Coverage remains the loaded cache and configured detail radius. */
    // P0-S5: grass is drawn to FadeRadius() (r__detail_radius x r4_detail_radius_scale);
    // its shadows only to the R2 distance r__detail_radius, shrinking over the
    // last 30% so casters never pop. Off-camera casters use the camera fade.
    const float camera_radius=CDetailManager::FadeRadius();
    const float radius=std::max(20.f,std::min(camera_radius,ps_r__Detail_radius));
    const float limit=radius*radius,fade_range=camera_radius*camera_radius-1.f;
    // P0-S11 PERF02: camera lists are r_items of cache_pool slots; mark them
    // by slot/part/variant in O(1) instead of sorting and binary-searching
    // the pointers. A list outside cache_pool keeps the sorted search.
    static std::vector<uint8_t> camera_mark;static std::vector<uint32_t> marked;
    camera_mark.resize(size_t(dm_cache_size)*dm_obj_in_slot*3,0);marked.clear();
    static std::vector<const CDetailManager::SlotItemVec*> foreign;foreign.clear();
    {const char* pool=reinterpret_cast<const char*>(&manager.cache_pool[0]);const size_t pool_bytes=sizeof(manager.cache_pool);
        for(size_t i=0;i<camera_parts.size();++i){const char* at=reinterpret_cast<const char*>(camera_parts[i]);bool found=false;
            if(at>=pool && at<pool+pool_bytes){const size_t slot=size_t(at-pool)/sizeof(CDetailManager::Slot);const CDetailManager::Slot& cell=manager.cache_pool[slot];
                for(unsigned part=0;part<dm_obj_in_slot && !found;++part)for(unsigned variant=0;variant<3;++variant)if(&cell.G[part].r_items[variant]==camera_parts[i]){
                    const uint32_t index=static_cast<uint32_t>((slot*dm_obj_in_slot+part)*3+variant);if(!camera_mark[index]){camera_mark[index]=1;marked.push_back(index);}found=true;break;}}
            if(!found)foreign.push_back(camera_parts[i]);}}
    std::sort(foreign.begin(),foreign.end(),std::less<const CDetailManager::SlotItemVec*>());
    struct Unmark {~Unmark(){for(size_t i=0;i<marked.size();++i)camera_mark[marked[i]]=0;marked.clear();}} unmark; // also on a throw
    // P0-S11 PERF02: per-mesh/variant buckets keep their capacity between frames.
    static std::vector<std::vector<ShadowDetailItem> > buckets;buckets.resize(manager.objects.size()*3);
    for(size_t i=0;i<buckets.size();++i)buckets[i].clear();
    frame.shadow_cells.resize(dm_cache_size);frame.shadow_radius=radius;
    size_t candidates=0;
    // Shadow topology covers the slot square around the camera slot that holds
    // the shadow radius. It moves with the cache in whole slots, so the 192 m
    // camera cache does not grow the shadow BVH or add rebuilds.
    const int camera_sx=iFloor(Device.vCameraPosition.x/dm_slot_size+.5f),camera_sz=iFloor(Device.vCameraPosition.z/dm_slot_size+.5f);
    const int shadow_slots=int(ceilf(radius/dm_slot_size))+1;
    // P0-S8: cache_Update keeps the grid centred on the camera slot, so only
    // its central window can hold shadow cells. Visiting that window instead
    // of all 37249 pool slots (dm_size 96) avoids streaming ~9 MB of slot
    // headers per frame; slots outside it stay not ready, as before.
    const int reach=std::min(dm_size,shadow_slots+2);
    for(int gz=dm_size-reach;gz<=dm_size+reach;++gz)for(int gx=dm_size-reach;gx<=dm_size+reach;++gx){
        const CDetailManager::Slot* pointer=manager.cache[gz][gx];
        if(pointer<manager.cache_pool || pointer>=manager.cache_pool+dm_cache_size)continue;
        const unsigned slot=static_cast<unsigned>(pointer-manager.cache_pool);const CDetailManager::Slot& cell=*pointer;
        ShadowCell& provenance=frame.shadow_cells[slot];provenance.sx=cell.sx;provenance.sz=cell.sz;
        provenance.ready=!cell.empty && cell.type==CDetailManager::stReady && abs(cell.sx-camera_sx)<=shadow_slots && abs(cell.sz-camera_sz)<=shadow_slots;
        provenance.center[0]=cell.vis.sphere.P.x;provenance.center[1]=cell.vis.sphere.P.y;provenance.center[2]=cell.vis.sphere.P.z;
        if(!provenance.ready)continue;
        const float dx=cell.vis.sphere.P.x-Device.vCameraPosition.x,dy=cell.vis.sphere.P.y-Device.vCameraPosition.y,dz=cell.vis.sphere.P.z-Device.vCameraPosition.z;
        const float distance=dx*dx+dy*dy+dz*dz;
        const bool within_radius=Finite(distance) && distance<=limit;
        const float fade=within_radius?(distance<1.f?1.f:1.f-(distance-1.f)/fade_range):0;
        const float edge=within_radius?std::min(1.f,(radius-sqrtf(distance))/(.3f*radius)):0;
        for(unsigned part=0;part<dm_obj_in_slot;++part){const CDetailManager::SlotPart& source=cell.G[part];
            if(source.id==DetailSlot::ID_Empty || source.id>=manager.objects.size())continue;
            // P0-S8: flat clutter (leaves) casts no shadow (P0-S5/S7); drop it
            // here instead of after bounds, BVH and selection for every view.
            if(source.id<meshes.size() && FlatGroundClutter(meshes[source.id]))continue;
            bool camera_part=false;
            for(unsigned variant=0;variant<3;++variant){const CDetailManager::SlotItemVec& items=source.r_items[variant];
                if(items.empty() || !(camera_mark[(size_t(slot)*dm_obj_in_slot+part)*3+variant] ||
                    (!foreign.empty() && std::binary_search(foreign.begin(),foreign.end(),&items,std::less<const CDetailManager::SlotItemVec*>()))))continue;
                camera_part=true;
                if(edge<=.02f)continue; // visible far grass casts no shadow
                for(size_t i=0;i<items.size();++i){
                    if(++candidates>KICH_R4_MAX_DETAIL_INSTANCES)throw std::runtime_error("detail shadow candidate safety limit");
                    const ShadowDetailItem candidate={items[i],items[i]->scale_calculated*edge,slot};
                    buckets[source.id*3+variant].push_back(candidate);}
            }
            // Preserve independent off-camera casters and their full configured
            // radius. Visible cached parts may straddle that radius until their
            // next update; their still-drawn geometry must retain its shadow.
            if(camera_part || edge<=.02f)continue;
            const float r=manager.objects[source.id]->bv_sphere.R;
            const float projected=r*r/std::max(distance,1.e-6f);
            for(size_t i=0;i<source.items.size();++i){const CDetailManager::SlotItem& item=*source.items[i];
                const float scale=item.scale*fade,ssa=scale*scale*projected;if(ssa<r_ssaDISCARD)continue;
                const unsigned variant=ssa>16*r_ssaDISCARD?std::min(2u,item.vis_ID):0u;
                if(++candidates>KICH_R4_MAX_DETAIL_INSTANCES)throw std::runtime_error("detail shadow candidate safety limit");
                const ShadowDetailItem candidate={&item,scale*edge,slot};buckets[source.id*3+variant].push_back(candidate);}
        }
    }
    for(size_t mesh=0;mesh<manager.objects.size();++mesh)for(unsigned variant=0;variant<3;++variant){const auto& items=buckets[mesh*3+variant];
        if(items.empty())continue;
        kich_r4_detail::Group group={static_cast<uint32_t>(mesh),variant,static_cast<uint32_t>(frame.shadow_instances.size()),static_cast<uint32_t>(items.size())};
        for(size_t i=0;i<items.size();++i)frame.AppendShadow(DetailInstance(*items[i].item,items[i].scale,variant),items[i].cell);
        frame.shadow_groups.push_back(group);}
    frame.prepare_ms=kich_r4_metrics::Elapsed(start);
}
// Moves a prepared frame's per-frame arrays into the published frame; meshes,
// generation and the legacy stream counters stay with the published one.
void Publish(kich_r4_detail::Frame& published,kich_r4_detail::Frame& prepared){
    published.groups.swap(prepared.groups);published.instances.swap(prepared.instances);
    published.shadow_groups.swap(prepared.shadow_groups);published.shadow_instances.swap(prepared.shadow_instances);
    published.shadow_cells.swap(prepared.shadow_cells);published.shadow_instance_cells.swap(prepared.shadow_instance_cells);
    memcpy(published.wave,prepared.wave,sizeof(published.wave));memcpy(published.wind,prepared.wind,sizeof(published.wind));
    memcpy(published.variants,prepared.variants,sizeof(published.variants));
    published.shadow_radius=prepared.shadow_radius;published.prepare_ms=prepared.prepare_ms;
}
/* P0-S11 PERF02: the instance assembly joins MT_CALC on the engine's parallel
 * thread (r4_mt_details), into a staging frame published by Details->Render.
 * Exactly one of the two paths prepares a frame: the lock and done_frame
 * decide; a frame whose detail calculation did not run here (MT_SYNC computed
 * it on the render thread) is prepared there, as before. */
struct ParallelPrepare {
    xrCriticalSection lock;CDetailManager* manager;kich_r4_detail::Frame staging;
    u32 done_frame;bool ready;std::string error;
    ParallelPrepare():manager(NULL),done_frame(u32(-1)),ready(false){}
    void __stdcall Run(){
        lock.Enter();
        CDetailManager* m=manager;
        if(m && done_frame!=Device.dwFrame && m->m_frame_calc==Device.dwFrame && m->m_frame_rendered!=Device.dwFrame && g_pGamePersistent){
            try{CDetailManager::SSwingValue swing;swing.lerp(m->swing_desc[0],m->swing_desc[1],g_pGamePersistent->Environment().wind_strength_factor);
                PrepareInto(*m,KichR4_DetailFrame().meshes,staging,swing);error.clear();}
            catch(const std::exception& e){error=e.what();}catch(...){error="detail prepare failed on the parallel thread";}
            done_frame=Device.dwFrame;ready=true;}
        lock.Leave();}
};
ParallelPrepare& Parallel(){static ParallelPrepare* parallel=new ParallelPrepare();return *parallel;} // outlives static teardown of the thread
}
fastdelegate::FastDelegate0<> KichR4_DetailParallel(CDetailManager* manager){ParallelPrepare& p=Parallel();p.manager=manager;return fastdelegate::FastDelegate0<>(&p,&ParallelPrepare::Run);}
void KichR4_DetailParallelStop(){ParallelPrepare& p=Parallel();p.lock.Enter();p.manager=NULL;p.ready=false;p.done_frame=u32(-1);p.lock.Leave();}
// Render thread, after MT_SYNC: publish the parallel result or prepare here.
void KichR4_DetailPrepare(CDetailManager& manager){
    kich_r4_detail::Frame& frame=KichR4_DetailFrame();ParallelPrepare& p=Parallel();
    p.lock.Enter();
    const bool parallel=p.ready && p.done_frame==Device.dwFrame && p.manager==&manager;
    std::string error;
    if(parallel){p.ready=false;error.swap(p.error);if(error.empty()){frame.ClearFrame(Device.dwFrame);Publish(frame,p.staging);kich_r4_queue::Global().Add(kich_r4_queue::DetailWorker,frame.prepare_ms);}}
    else{p.done_frame=Device.dwFrame;p.ready=false; // a late parallel run must not repeat this frame
        try{PrepareInto(manager,frame.meshes,frame,manager.swing_current);}catch(...){p.lock.Leave();throw;}}
    p.lock.Leave();
    if(!error.empty())throw std::runtime_error(error);
}
