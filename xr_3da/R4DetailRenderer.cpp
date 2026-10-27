#include "KichR4NativePrelude.h"
#include "KichR4DetailData.h"
#include "KichR4Native.h"
#include "KichR4FrameMetrics.h"
#include "KichR4QueueProfile.h"
#include "KichR4ParallelFor.h"
#include "RenderSnapshot.h"
#include "IGame_Persistent.h"
#include "Environment.h"
#include <functional>
#include <atomic>
extern float r_ssaDISCARD;
extern int ps_r4_detail_threads; // P0-S11 PERF02: R4 helper threads of the detail assembly

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
namespace {CDetailManager* deferred_manager=NULL;u32 deferred_frame=u32(-1);} // P0-S11 KichR4_DetailDefer
void KichR4_DetailUnload(){KichR4_DetailFrame().Reset();deferred_manager=NULL;deferred_frame=u32(-1);}
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
// Grass casts to the R2 distance r__detail_radius (at least 20 m, never past
// the drawn FadeRadius). Settings only: known before the frame is assembled.
float ShadowRadius(){return std::max(20.f,std::min(CDetailManager::FadeRadius(),ps_r__Detail_radius));}
/* P0-S11 PERF02: R4's helper threads (r4_detail_threads, KichR4ParallelFor.h)
 * share the assembly. Work is cut into contiguous runs at precomputed
 * offsets, so every instance lands at the index the serial loop gives it and
 * the frame is the same for any thread count. */
kich_r4_parallel::Pool& AssemblyPool(){return kich_r4_parallel::Shared();}
// Also published for the render thread's detail shadow selection.
unsigned AssemblyHelpers(){const unsigned helpers=static_cast<unsigned>(std::max(0,std::min(ps_r4_detail_threads,8)));
    kich_r4_parallel::SharedHelpers().store(helpers);return helpers;}
using kich_r4_parallel::Cuts;
// Fills frame's per-frame arrays from the manager's visibility lists of this
// frame (consumed) and its cache. meshes: the loaded, immutable mesh table.
// The scratch below is shared by its two callers, which never overlap.
void PrepareInto(CDetailManager& manager,const std::vector<kich_r4_detail::Mesh>& meshes,kich_r4_detail::Frame& frame,const CDetailManager::SSwingValue& swing){
    using namespace kich_r4_detail;
    const auto start=kich_r4_metrics::Start(g_render_snapshot_active);frame.ClearFrame(Device.dwFrame);
    const unsigned helpers=AssemblyHelpers();const size_t parts=kich_r4_parallel::Parts(helpers);
    static std::vector<size_t> cuts;static std::vector<const char*> errors; // per chunk: the first rejected instance
    // A rejected instance fails the whole frame, as the serial Validate did;
    // the earliest chunk's reason is the serial loop's first one.
    auto fail=[&]{for(size_t c=0;c<errors.size();++c)if(errors[c]){const char* reason=errors[c];frame.ClearFrame(Device.dwFrame);throw std::runtime_error(reason);}};
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
    // Camera instances: lay out the groups, then fill the runs in parallel.
    struct VisibleRun {const CDetailManager::SlotItemVec* items;size_t first;unsigned variant;};
    static std::vector<VisibleRun> runs;runs.clear();size_t total=0;
    for(unsigned variant=0;variant<3;++variant){
        if(variant){const unsigned n=variant-1;
            frame.wave[variant][0]=(variant==1?1.f/5:1.f/3)/PI_MUL_2;
            frame.wave[variant][1]=(1.f/7)/PI_MUL_2;frame.wave[variant][2]=(variant==1?1.f/3:1.f/5)/PI_MUL_2;
            frame.wave[variant][3]=Device.fTimeGlobal*swing.speed/PI_MUL_2;
            frame.wind[variant][0]=sinf(rotations[n])*amplitudes[n];frame.wind[variant][2]=cosf(rotations[n])*amplitudes[n];}
        for(size_t mesh=0;mesh<manager.objects.size();++mesh){
            CDetailManager::vis_list& list=manager.m_visibles[variant];
            kich_r4_detail::Group group={static_cast<uint32_t>(mesh),variant,static_cast<uint32_t>(total),0};
            for(size_t slot=0;slot<list[mesh].size();++slot){const CDetailManager::SlotItemVec& items=*list[mesh][slot];
                if(items.empty())continue;
                camera_parts.push_back(&items);const VisibleRun run={&items,total,variant};runs.push_back(run);total+=items.size();
                if(total>KICH_R4_MAX_DETAIL_INSTANCES)throw std::runtime_error("detail instance safety limit");}
            group.count=static_cast<uint32_t>(total-group.first);
            if(group.count){frame.groups.push_back(group);frame.variants[variant]+=group.count;}
        }
    }
    frame.instances.resize(total);
    Cuts(runs.size(),total,parts,[&](size_t i){return runs[i].first;},cuts);errors.assign(cuts.size()-1,NULL);
    {KichR4DetailInstance* out=frame.instances.data();
        AssemblyPool().For(errors.size(),helpers,[&](size_t c){
            for(size_t r=cuts[c];r<cuts[c+1];++r){const VisibleRun& run=runs[r];const CDetailManager::SlotItemVec& items=*run.items;
                for(size_t i=0;i<items.size();++i){const CDetailManager::SlotItem& item=*items[i];KichR4DetailInstance& v=out[run.first+i];
                    v=DetailInstance(item,item.scale_calculated,run.variant);if(!errors[c])errors[c]=Frame::Invalid(v);}}});}
    fail();
    for(unsigned variant=0;variant<3;++variant)for(size_t mesh=0;mesh<manager.objects.size();++mesh)manager.m_visibles[variant][mesh].clear_not_free();
    /* X-Ray's detail cache owns original item transforms independently of the
     * camera-visible r_items. Gather ready cells without a camera-frustum test;
     * each shadow pass will subsequently cull these compact instances itself.
     * Coverage remains the loaded cache and configured detail radius. */
    // P0-S5: grass is drawn to FadeRadius() (r__detail_radius x r4_detail_radius_scale);
    // its shadows only to the R2 distance r__detail_radius, shrinking over the
    // last 30% so casters never pop. Off-camera casters use the camera fade.
    const float camera_radius=CDetailManager::FadeRadius();
    const float radius=ShadowRadius();
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
    frame.shadow_cells.resize(dm_cache_size);frame.shadow_radius=radius;
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
    // P0-S11 PERF02: the window's rows are cut into contiguous chunks, each
    // with its own per-mesh/variant buckets (capacity kept between frames).
    // Concatenated in row order they are the serial loop's buckets. Each cache
    // slot appears once in the grid, so chunks write distinct shadow_cells.
    struct ShadowChunk {std::vector<std::vector<ShadowDetailItem> > buckets;size_t candidates;bool overflow;};
    static std::vector<ShadowChunk> chunks;
    const size_t rows=size_t(2*reach+1),chunk_count=std::min(rows,parts);
    if(chunks.size()<chunk_count)chunks.resize(chunk_count);
    for(size_t c=0;c<chunk_count;++c){ShadowChunk& chunk=chunks[c];chunk.buckets.resize(manager.objects.size()*3);
        for(size_t i=0;i<chunk.buckets.size();++i)chunk.buckets[i].clear();chunk.candidates=0;chunk.overflow=false;}
    AssemblyPool().For(chunk_count,helpers,[&](size_t c){ShadowChunk& chunk=chunks[c];std::vector<std::vector<ShadowDetailItem> >& buckets=chunk.buckets;
    const int row_first=dm_size-reach+int(c*rows/chunk_count),row_end=dm_size-reach+int((c+1)*rows/chunk_count);
    for(int gz=row_first;gz<row_end;++gz)for(int gx=dm_size-reach;gx<=dm_size+reach;++gx){
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
                    if(++chunk.candidates>KICH_R4_MAX_DETAIL_INSTANCES){chunk.overflow=true;return;}
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
                if(++chunk.candidates>KICH_R4_MAX_DETAIL_INSTANCES){chunk.overflow=true;return;}
                const ShadowDetailItem candidate={&item,scale*edge,slot};buckets[source.id*3+variant].push_back(candidate);}
        }
    }});
    {size_t candidates=0;for(size_t c=0;c<chunk_count;++c)if(chunks[c].overflow || (candidates+=chunks[c].candidates)>KICH_R4_MAX_DETAIL_INSTANCES)
        throw std::runtime_error("detail shadow candidate safety limit");}
    // Shadow groups per mesh/variant in the serial order; each chunk's bucket
    // is one run. The fill also computes the instance bounds the shadow BVH
    // needs (Frame::shadow_bounds), off the render thread.
    struct ShadowRun {const std::vector<ShadowDetailItem>* items;size_t first;uint32_t mesh,variant;};
    static std::vector<ShadowRun> shadow_runs;shadow_runs.clear();size_t shadow_total=0;
    for(size_t mesh=0;mesh<manager.objects.size();++mesh)for(unsigned variant=0;variant<3;++variant){const size_t first=shadow_total;
        for(size_t c=0;c<chunk_count;++c){const std::vector<ShadowDetailItem>& items=chunks[c].buckets[mesh*3+variant];if(items.empty())continue;
            const ShadowRun run={&items,shadow_total,static_cast<uint32_t>(mesh),variant};shadow_runs.push_back(run);shadow_total+=items.size();}
        if(shadow_total==first)continue;
        const kich_r4_detail::Group group={static_cast<uint32_t>(mesh),variant,static_cast<uint32_t>(first),static_cast<uint32_t>(shadow_total-first)};
        frame.shadow_groups.push_back(group);}
    frame.shadow_instances.resize(shadow_total);frame.shadow_instance_cells.resize(shadow_total);frame.shadow_bounds.resize(shadow_total);
    Cuts(shadow_runs.size(),shadow_total,parts,[&](size_t i){return shadow_runs[i].first;},cuts);errors.assign(cuts.size()-1,NULL);
    {KichR4DetailInstance* out=frame.shadow_instances.data();uint32_t* cells=frame.shadow_instance_cells.data();Bounds* bounds=frame.shadow_bounds.data();
        AssemblyPool().For(errors.size(),helpers,[&](size_t c){
            for(size_t r=cuts[c];r<cuts[c+1];++r){const ShadowRun& run=shadow_runs[r];const std::vector<ShadowDetailItem>& items=*run.items;
                const Mesh* mesh=run.mesh<meshes.size()?&meshes[run.mesh]:NULL;
                for(size_t i=0;i<items.size();++i){const size_t at=run.first+i;KichR4DetailInstance& v=out[at];
                    v=DetailInstance(*items[i].item,items[i].scale,run.variant);cells[at]=items[i].cell;
                    if(!errors[c])errors[c]=Frame::Invalid(v);
                    bounds[at]=mesh?InstanceBounds(*mesh,v,run.variant,frame.wind):Bounds();}}});}
    fail();
    frame.prepare_ms=kich_r4_metrics::Elapsed(start);
}
// Moves a prepared frame's per-frame arrays into the published frame; meshes,
// generation and the legacy stream counters stay with the published one.
void Publish(kich_r4_detail::Frame& published,kich_r4_detail::Frame& prepared){
    published.groups.swap(prepared.groups);published.instances.swap(prepared.instances);
    published.shadow_groups.swap(prepared.shadow_groups);published.shadow_instances.swap(prepared.shadow_instances);
    published.shadow_cells.swap(prepared.shadow_cells);published.shadow_instance_cells.swap(prepared.shadow_instance_cells);
    published.shadow_bounds.swap(prepared.shadow_bounds);
    memcpy(published.wave,prepared.wave,sizeof(published.wave));memcpy(published.wind,prepared.wind,sizeof(published.wind));
    memcpy(published.variants,prepared.variants,sizeof(published.variants));
    published.shadow_radius=prepared.shadow_radius;published.prepare_ms=prepared.prepare_ms;
}
/* P0-S11 PERF02: the instance assembly joins MT_CALC on the engine's parallel
 * thread (r4_mt_details), into a staging frame published by the frame's first
 * reader (KichR4_DetailFinish).
 * Exactly one of the two paths prepares a frame: the lock and done_frame
 * decide; a frame whose detail calculation did not run here (MT_SYNC computed
 * it on the render thread) is prepared there, as before. */
struct ParallelPrepare {
    xrCriticalSection lock;CDetailManager* manager;kich_r4_detail::Frame staging;
    u32 done_frame;bool ready;std::string error;
    // The frame Details->Render deferred, set without the lock (Render must not
    // wait for a running assembly). It is stored before m_frame_rendered, so a
    // job that sees the frame rendered also sees it deferred: a job that starts
    // after Details->Render still assembles, unless the first reader took it.
    std::atomic<u32> deferred;
    ParallelPrepare():manager(NULL),done_frame(u32(-1)),ready(false),deferred(u32(-1)){}
    void __stdcall Run(){
        lock.Enter();
        CDetailManager* m=manager;const u32 now=Device.dwFrame;
        if(m && done_frame!=now && m->m_frame_calc==now && (m->m_frame_rendered!=now || deferred.load()==now) && g_pGamePersistent){
            try{CDetailManager::SSwingValue swing;swing.lerp(m->swing_desc[0],m->swing_desc[1],g_pGamePersistent->Environment().wind_strength_factor);
                PrepareInto(*m,KichR4_DetailFrame().meshes,staging,swing);error.clear();}
            catch(const std::exception& e){error=e.what();}catch(...){error="detail prepare failed on the parallel thread";}
            done_frame=Device.dwFrame;ready=true;}
        lock.Leave();}
};
ParallelPrepare& Parallel(){static ParallelPrepare* parallel=new ParallelPrepare();return *parallel;} // outlives static teardown of the thread
}
fastdelegate::FastDelegate0<> KichR4_DetailParallel(CDetailManager* manager){ParallelPrepare& p=Parallel();p.manager=manager;return fastdelegate::FastDelegate0<>(&p,&ParallelPrepare::Run);}
void KichR4_DetailParallelStop(){ParallelPrepare& p=Parallel();p.lock.Enter();p.manager=NULL;p.ready=false;p.done_frame=u32(-1);p.deferred.store(u32(-1));p.lock.Leave();}
namespace {void FinishDeferred(){KichR4_DetailFinish();}}
// Details->Render (render thread, after MT_SYNC). The published frame gets its
// settings-only shadow radius now: preflight reads it before the detail block.
void KichR4_DetailDefer(CDetailManager& manager){
    deferred_manager=&manager;deferred_frame=Device.dwFrame;Parallel().deferred.store(Device.dwFrame);KichR4_DetailFrame().shadow_radius=ShadowRadius();
    kich_r4_detail::FinishFrameHook()=FinishDeferred;}
// The first reader of this frame's instances; later calls do nothing.
void KichR4_DetailFinish(){
    if(!deferred_manager || deferred_frame!=Device.dwFrame)return;
    CDetailManager& manager=*deferred_manager;deferred_manager=NULL;
    const auto stamp=kich_r4_metrics::Start(g_render_snapshot_active);KichR4_DetailPrepare(manager);
    kich_r4_queue::Global().AddFinished(kich_r4_queue::DetailPrepare,kich_r4_metrics::Elapsed(stamp));}
// Publish the parallel result or prepare here (also the direct test entry).
void KichR4_DetailPrepare(CDetailManager& manager){
    kich_r4_detail::Frame& frame=KichR4_DetailFrame();ParallelPrepare& p=Parallel();
    p.lock.Enter();
    const bool parallel=p.ready && p.done_frame==Device.dwFrame && p.manager==&manager;
    std::string error;
    if(parallel){p.ready=false;error.swap(p.error);if(error.empty()){frame.ClearFrame(Device.dwFrame);Publish(frame,p.staging);kich_r4_queue::Global().AddFinished(kich_r4_queue::DetailWorker,frame.prepare_ms);}}
    else{p.done_frame=Device.dwFrame;p.ready=false; // a late parallel run must not repeat this frame
        try{PrepareInto(manager,frame.meshes,frame,manager.swing_current);}catch(...){p.lock.Leave();throw;}}
    p.lock.Leave();
    if(!error.empty())throw std::runtime_error(error);
}
