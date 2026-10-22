#include "KichR4NativePrelude.h"
#include "KichR4DetailData.h"
#include "KichR4Native.h"
#include <chrono>
extern float r_ssaDISCARD;

namespace {
struct ShadowDetailItem {const CDetailManager::SlotItem* item;float scale;};
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
}
void KichR4_DetailPrepare(CDetailManager& manager){
    using namespace kich_r4_detail;
    const auto start=std::chrono::steady_clock::now();Frame& frame=KichR4_DetailFrame();frame.ClearFrame(Device.dwFrame);
    const CDetailManager::SSwingValue& swing=manager.swing_current;
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
    const float radius=std::max(20.f,std::min(dm_fade,ps_r__Detail_radius));
    const float limit=radius*radius,fade_range=limit-1.f;
    std::vector<std::vector<ShadowDetailItem> > buckets(manager.objects.size()*3);
    size_t candidates=0;
    for(unsigned slot=0;slot<dm_cache_size;++slot){const CDetailManager::Slot& cell=manager.cache_pool[slot];
        if(cell.empty || cell.type!=CDetailManager::stReady)continue;
        const float dx=cell.vis.sphere.P.x-Device.vCameraPosition.x,dy=cell.vis.sphere.P.y-Device.vCameraPosition.y,dz=cell.vis.sphere.P.z-Device.vCameraPosition.z;
        const float distance=dx*dx+dy*dy+dz*dz;if(!Finite(distance) || distance>limit)continue;
        const float fade=distance<1.f?1.f:1.f-(distance-1.f)/fade_range;
        for(unsigned part=0;part<dm_obj_in_slot;++part){const CDetailManager::SlotPart& source=cell.G[part];
            if(source.id==DetailSlot::ID_Empty || source.id>=manager.objects.size())continue;
            const float r=manager.objects[source.id]->bv_sphere.R;
            const float projected=r*r/std::max(distance,1.e-6f);
            for(size_t i=0;i<source.items.size();++i){const CDetailManager::SlotItem& item=*source.items[i];
                const float scale=item.scale*fade,ssa=scale*scale*projected;if(ssa<r_ssaDISCARD)continue;
                const unsigned variant=ssa>16*r_ssaDISCARD?std::min(2u,item.vis_ID):0u;
                if(++candidates>KICH_R4_MAX_DETAIL_INSTANCES)throw std::runtime_error("detail shadow candidate safety limit");
                const ShadowDetailItem candidate={&item,scale};buckets[source.id*3+variant].push_back(candidate);}
        }
    }
    for(size_t mesh=0;mesh<manager.objects.size();++mesh)for(unsigned variant=0;variant<3;++variant){const auto& items=buckets[mesh*3+variant];
        if(items.empty())continue;
        kich_r4_detail::Group group={static_cast<uint32_t>(mesh),variant,static_cast<uint32_t>(frame.shadow_instances.size()),static_cast<uint32_t>(items.size())};
        for(size_t i=0;i<items.size();++i)frame.AppendShadow(DetailInstance(*items[i].item,items[i].scale,variant));
        frame.shadow_groups.push_back(group);}
    frame.prepare_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
