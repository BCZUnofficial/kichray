// Execute real pass planning against the recording API; no GPU pixel claims.
#include "recording.h"

namespace {
std::map<KichR4Handle,std::pair<uint64_t,uint64_t> > observed;
bool fail_shadow=false;
int32_t ObserveDraw(KichR4Handle device,const KichR4DeferredDrawDesc* d){
    const bool shadow=active_pass.color_count==0 && active_pass.depth!=0;
    if(shadow && fail_shadow)return error;
    const int32_t result=DDraw(device,d);
    if(!result && shadow){auto& value=observed[active_pass.depth];++value.first;value.second+=d->state.draw.index_count;}
    return result;
}
struct Trace {std::vector<KichR4DeferredDrawDesc> draws;std::vector<kich_r4_lighting::Constants> constants;std::vector<KichR4PassDesc> render_passes;int calls;};
Trace Render(bool enabled,bool fail=false){
    Fixture fixture;f=&fixture;ResetDeferredRecorder();observed.clear();fail_shadow=fail;
    kich_r4_record::Request(enabled);kich_r4_record::BeginFrame(1);
    auto apis=DeferredAPIs();apis.deferred.draw=ObserveDraw;
    const KichR4LevelFiles files={Open,Close};kich_r4_game::Session game(apis,files,"l01_escape",NULL);
    game.deferred.native_mode=true;auto input=DeferredInput();input.engine_frame=1;input.lighting.count=1;
    input.lighting.lights.resize(1);input.lighting.sources.resize(1);
    input.lighting.shadows.assign(1,1);
    auto& light=input.lighting.lights[0];light.type=1;light.range=15;light.position[1]=2;light.color[0]=1;
    input.lighting.sources[0].serial=17;
    const int32_t result=game.Render(input);
    const auto& record=game.deferred.shadow_record;
    Require(kich_r4_shadow_record::Active()==NULL,"pass observer cannot escape completed/failed render scope");
    Require(record.valid==enabled,"disabled recording cannot expose stale frame statistics");
    if(fail){
        Require(result<0 && !game.frame.rendered,"failed shadow submission cannot present a frame");
        Require(record.sun[0].begun && !record.sun[0].completed,"started pass is distinct from completed shadow");
        Require(record.sun[0].work[kich_r4_shadow_record::Static].draws==0,"failed draw is not counted as a submitted caster");
    }else{
        Require(result==1,"recording switch preserves real native frame submission");
        if(enabled){
            Require(record.frame==1 && record.sun[0].completed && record.sun[1].completed,"both native cascades refer to the rendered engine frame");
            Require(record.sun[0].span==128 && record.sun[1].span==512 && record.sun[0].texel_size==.0625f && record.sun[1].texel_size==.25f,"reports actual projection density instead of map size alone");
            for(unsigned cascade=0;cascade<2;++cascade){const auto& pass=record.sun[cascade];const auto& work=pass.work[kich_r4_shadow_record::Static];
                const auto actual=observed.at(pass.map);
                Require(pass.resolution==2048 && pass.generation==1,"sun image size and allocation generation are explicit");
                Require(work.draws==actual.first && work.indices==actual.second && work.casters==work.draws,"cascade counts match successful API submissions, not candidate list size");
                Require(!memcmp(pass.matrix,game.deferred.base.shadow[cascade].m,sizeof(pass.matrix)),"reported matrix is the one bound for shadow and receiver projection");
            }
            Require(record.local_attempted==6 && record.local_completed==6 && record.local_resolution==512,"point shadows report six completed faces separately");
            uint64_t draws=0,indices=0;for(const auto& item:observed)if(item.first!=record.sun[0].map && item.first!=record.sun[1].map){draws+=item.second.first;indices+=item.second.second;}
            Require(record.local[kich_r4_shadow_record::Static].draws==draws && record.local[kich_r4_shadow_record::Static].indices==indices,"local totals match all submitted point faces");
        }
    }
    const Trace trace={draw_descriptions,draw_constants,passes,f->calls};fail_shadow=false;
    KichR4GeometryReport report={};Require(game.Stop(report)==0 && f->objects.empty() && !f->leases,"observer never retains GPU resources/readers");
    kich_r4_record::Request(false);kich_r4_record::BeginFrame(2);return trace;
}
void Signatures(){
    using namespace kich_r4_shadow_record;
    Work a,b;a.Caster(3,0,6);a.Caster(9,3,12);b.Caster(9,3,12);b.Caster(3,0,6);
    Require(a.id_sum==b.id_sum && a.id_xor==b.id_xor && a.range_sum==b.range_sum,"set signatures ignore traversal order");
    Work changed;changed.Caster(3,0,6);changed.Caster(10,3,12);
    Require(a.id_sum!=changed.id_sum || a.id_xor!=changed.id_xor,"caster-set change is distinguishable from submission count");
    Work lod;lod.Caster(3,0,3);lod.Caster(9,3,12);
    Require(a.id_sum==lod.id_sum && a.id_xor==lod.id_xor && a.range_sum!=lod.range_sum,"LOD/range change is separate from caster identity change");
    Work unknown;unknown.Caster(0,12,3,false);Require(unknown.casters==1 && unknown.unknown_ids==1 && !unknown.id_sum && !unknown.id_xor,"unknown compatibility identity is not invented from command ordinal");
    Pass outer,inner;{Scope first(&outer);Require(Active()==&outer,"observer installed");{Scope second(&inner);Require(Active()==&inner,"nested scope installed");}Require(Active()==&outer,"nested scope restored");}Require(!Active(),"root scope restored");
    Require(DetailID(1,2,3,4)!=DetailID(1,2,3,5) && DetailID(1,2,3,4)!=DetailID(2,2,3,4),"detail identity includes original placement and mesh");
}
}
int main(){try{(void)&Scenario;Signatures();const Trace off=Render(false),on=Render(true);
    Require(off.calls==on.calls && off.draws.size()==on.draws.size() && off.render_passes.size()==on.render_passes.size(),"log recording adds no rendering/resource operations");
    for(size_t i=0;i<off.draws.size();++i){const auto& a=off.draws[i];const auto& b=on.draws[i];
        Require(a.state.draw.pipeline==b.state.draw.pipeline && a.state.draw.index_count==b.state.draw.index_count && a.state.draw.start_index==b.state.draw.start_index,"recording preserves submission pipeline and geometry range");
        Require(!memcmp(&off.constants[i],&on.constants[i],sizeof(off.constants[i])),"recording does not alter lighting or shadow constants");
    }
    Render(true,true);
    std::printf("E1.7 shadow recorder: %d checks passed (real planner, successful submissions, no GPU pixels)\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
