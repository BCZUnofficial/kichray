#include "recording.h"
#include "../../xr_3da/KichR4TexturePreflight.h"
namespace {
int32_t ReleaseTexture(KichR4Handle,KichR4Handle handle){Require(f->objects.erase(handle)==1,"release live texture once");++f->released;return 0;}
void Setup(KichR4LevelSession& level,KichR4FrameSession& frame){Configure(level,frame);level.bridge.resources.release_resource=ReleaseTexture;
    Require(frame.Start(reinterpret_cast<void*>(1),640,480)==0 && level.Placeholder(frame,false,&level.white)==0 && level.Placeholder(frame,true,&level.checker)==0,"prepared texture setup");}
void Stop(KichR4LevelSession& level,KichR4FrameSession& frame){KichR4GeometryReport report={};
    Require(level.bridge.Stop(frame,&report)==0 && f->objects.empty() && f->opens==f->closes && !f->leases,"prepared texture GPU/reader ownership balances");
    Require(level.prepared_texture_bytes==0 && level.prepared_textures.empty(),"transaction releases prepared CPU input");}
void SingleRead(){Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
    level.NeedTexture("base",kich_r4_texture::BASE);level.NeedTexture("base.dds",kich_r4_texture::UI);level.NeedTexture("leaf",kich_r4_texture::SHADOW_ALPHA);
    Require(f->opens==2 && f->closes==2 && !f->leases,"metadata readers close immediately and canonical aliases share their source");
    Require(level.prepared_texture_bytes>0 && level.prepared_texture_bytes<=level.PreparedTextureLimit(),"cold input is bounded while awaiting residency plan");
    Require(level.CommitTexturePlan(frame)==0 && f->opens==2 && f->texture_calls==4,"metadata and planned upload share one DDS read per cold source");
    Require(level.Texture(frame,"base.dds")!=level.checker && level.Texture(frame,"leaf")!=level.checker,"prepared sources resolve through normal registry");
    const auto calls=f->texture_calls;level.texture_registry.Begin();level.NeedTexture("base",kich_r4_texture::BASE);level.NeedTexture("leaf",kich_r4_texture::BASE);
    Require(level.CommitTexturePlan(frame,true)==0 && f->opens==2 && f->texture_calls==calls && !f->progress,"hot frame neither rereads nor uploads nor pumps loading callbacks");Stop(level,frame);
}
void Capacity(){Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
    level.texture_registry.budget=4096;f->files["$game_textures$/base.dds"]=fixture::Dds(8,8,4);f->files["$game_textures$/leaf.dds"]=fixture::Dds(8,8,4);f->files["$game_textures$/large.dds"]=fixture::Dds(16,16);
    level.NeedTexture("base",kich_r4_texture::UI);level.NeedTexture("leaf",kich_r4_texture::BASE);level.NeedTexture("large",kich_r4_texture::BASE);
    Require(f->opens==3 && f->opens==f->closes && level.prepared_texture_bytes==468 && level.PreparedTextureLimit()==512,"prepared storage respects CPU fraction, including header, and closes oversized/overflow sources");
    Require(level.CommitTexturePlan(frame)==0 && f->opens==5,"overflow and oversized inputs safely reopen while retained input avoids the second read");
    for(const char* name:{"base","leaf","large"})Require(level.Texture(frame,name)!=level.checker,"CPU cache cap never denies a GPU-resident dependency");
    Require(level.texture_registry.bytes<=level.texture_registry.budget,"prepared input does not change GPU admission budget");Stop(level,frame);
}
void EpochAndBudget(){Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
    level.NeedTexture("base",kich_r4_texture::BASE);level.texture_registry.Begin();level.NeedTexture("leaf",kich_r4_texture::BASE);
    Require(level.prepared_textures.size()==1 && !level.prepared_textures.count("base"),"abandoned epoch input does not survive into a new texture request");
    level.texture_registry.budget=0;Require(level.CommitTexturePlan(frame)==0 && f->opens==2 && level.texture_registry.planned_denied==1,"budget reduction drops prepared memory and preserves denied-plan fallback");
    Require(level.Texture(frame,"leaf")==level.checker,"denied source never uploads outside plan");Stop(level,frame);
}
void ManySmallSources(){Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
    for(unsigned i=0;i<129;++i){const std::string name="small_"+std::to_string(i);f->files["$game_textures$/"+name+".dds"]=fixture::Dds(1,1);level.NeedTexture(name,kich_r4_texture::UI);}
    Require(level.prepared_textures.size()==128 && f->opens==129 && f->opens==f->closes,"many tiny sources also respect a finite prepared-entry bound");
    Require(level.CommitTexturePlan(frame)==0 && f->opens==130 && f->texture_calls==131 && !level.texture_registry.planned_denied,"entry overflow remains a valid ordinary upload without dropping UI textures");Stop(level,frame);
}
void LevelReset(){Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
    level.NeedTexture("base",kich_r4_texture::BASE);
    level.texture_override=[](void*,const std::string&,bool& handled)->KichR4Handle{handled=false;return 0;};
    Require(level.Start(frame,"l01_escape")==0 && !level.prepared_texture_bytes && level.prepared_textures.empty(),"level startup discards prepared input from an earlier attempt");Stop(level,frame);
}
void Cancellation(){Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
    level.NeedTexture("base",kich_r4_texture::BASE);level.NeedTexture("leaf",kich_r4_texture::BASE);f->cancel_at=1;
    Require(level.CommitTexturePlan(frame,true)==KICH_R4_FRAME_SUSPENDED && level.cancelled && !frame.failure && f->texture_calls==2,"loading cancellation stops before upload without marking device failure");
    Require(!level.prepared_texture_bytes && level.prepared_textures.empty() && !f->leases,"cancelled plan releases all prepared input");
    f->cancel_at=0;Require(level.CommitTexturePlan(frame,true)==0 && f->texture_calls==4 && f->progress==3 && f->opens==4,"cancelled plan can retry through ordinary source reads with one loading callback per upload");Stop(level,frame);
}
void UploadFailures(){
    {Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
        level.NeedTexture("base",kich_r4_texture::BASE);level.NeedTexture("leaf",kich_r4_texture::BASE);f->fail=f->calls+1;
        Require(level.CommitTexturePlan(frame)==error && frame.failure==error && f->opens==2,"device removal preserves original failure and does not reread prepared bytes");Stop(level,frame);}
    {Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
        level.NeedTexture("base",kich_r4_texture::BASE);level.NeedTexture("leaf",kich_r4_texture::BASE);f->fail=f->calls+1;f->failure=KICH_R4_E_MEMORY;
        Require(level.CommitTexturePlan(frame)==0 && !frame.failure && level.Texture(frame,"base")==level.checker && level.Texture(frame,"leaf")!=level.checker,"optional upload failure retains checker/retry without blocking other dependencies");
        Require(!level.prepared_texture_bytes,"failed optional upload cannot retain an old prepared copy");level.texture_registry.Begin();level.texture_registry.Begin();
        Require(level.Texture(frame,"base")!=level.checker && f->opens==3,"retry reopens fresh source after prepared transaction ends");Stop(level,frame);}
}
int32_t ThrowUpload(KichR4Handle,const KichR4DdsDesc*,const void*,uint32_t,KichR4Handle*){throw std::runtime_error("test upload exception");}
void Exceptions(){Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
    level.NeedTexture("base",kich_r4_texture::BASE);level.NeedTexture("leaf",kich_r4_texture::BASE);level.api.create_texture_dds=ThrowUpload;
    bool caught=false;try{level.CommitTexturePlan(frame);}catch(const std::runtime_error&){caught=true;}
    Require(caught && !level.prepared_texture_bytes && !f->leases,"exception unwinds current and pending prepared payloads");Stop(level,frame);
}
void PreflightLoading(){Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
    kich_r4_capture::Batch batch;kich_r4_capture::Style style;batch.commands.push_back({0,3,2,"base",style});batch.commands.push_back({0,3,2,"leaf",style});
    KichR4GameFrame input={};KichR4DetailSession details;f->cancel_at=2;
    Require(kich_r4_texture::Preflight(frame,level,batch,input,details,false,NULL,true)==KICH_R4_FRAME_SUSPENDED && f->texture_calls==3 && !frame.failure,"production preflight propagates loading cancellation between uploads");
    Require(!level.prepared_texture_bytes && f->opens==f->closes,"production preflight clears remaining input on partial warmup");
    f->cancel_at=0;Require(kich_r4_texture::Preflight(frame,level,batch,input,details,false,NULL,true)==0 && f->texture_calls==4,"partial warmup retry keeps already resident source");Stop(level,frame);
}
void ProbeGating(){Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
    kich_r4_capture::Batch batch;kich_r4_capture::Style style;batch.commands.push_back({0,3,2,"base",style});
    KichR4GameFrame input={};KichR4DetailSession details;
    kich_r4_record::Request(false);kich_r4_record::BeginFrame(1);
    Require(kich_r4_texture::Preflight(frame,level,batch,input,details,false)==0,"OFF retains real preflight and upload");
    Require(!level.preflight_probe.measured && !level.texture_probe && level.texture_io_ms==0 && level.texture_parse_ms==0 && level.texture_create_ms==0,
        "OFF does no diagnostic texture timing or probe collection");
    level.texture_registry.Begin();batch.commands.push_back({0,3,2,"leaf",style});
    kich_r4_record::Request(true);kich_r4_record::BeginFrame(2);
    Require(kich_r4_texture::Preflight(frame,level,batch,input,details,false)==0,"ON retains same texture plan");
    const auto on=level.preflight_probe;
    Require(on.measured && on.opens==1 && on.prepared_hits==1 && on.uploads==1 && on.resident_hits==1 && on.metadata_hits==1,
        "probe distinguishes retained residency and prepared cold-source upload without extra reads");
    Require(on.source_bytes==on.prepared_bytes && on.upload_bytes>0 && !on.open_failures && !on.upload_failures && !on.evictions && !level.texture_probe,
        "probe counts actual bytes and releases scope after preflight");
    level.texture_registry.Begin();kich_r4_record::Request(false);kich_r4_record::BeginFrame(3);
    const double io=level.texture_io_ms,parse=level.texture_parse_ms,upload=level.texture_create_ms;
    Require(kich_r4_texture::Preflight(frame,level,batch,input,details,false)==0 && !level.preflight_probe.measured && !level.preflight_probe.uploads && !level.preflight_probe.metadata_hits,
        "OFF resets stale frame probe even for hot sources");
    Require(io==level.texture_io_ms && parse==level.texture_parse_ms && upload==level.texture_create_ms,"OFF leaves cumulative diagnostic timings unchanged");
    g_render_snapshot_active=true;level.texture_registry.Begin();
    Require(kich_r4_texture::Preflight(frame,level,batch,input,details,false)==0 && level.preflight_probe.measured && level.preflight_probe.resident_hits==2,
        "explicit snapshot captures hot preflight with ordinary recording OFF");
    g_render_snapshot_active=false;
    Require(!level.texture_measure_snapshot && !level.texture_probe,"snapshot scope cannot leak recording into subsequent draws");Stop(level,frame);
}

void ProbeFailureScopes(){
    for(unsigned mode=0;mode<2;++mode){Fixture fixture;f=&fixture;KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame);
        kich_r4_capture::Batch batch;kich_r4_capture::Style style;batch.commands.push_back({0,3,2,"base",style});
        KichR4GameFrame input={};KichR4DetailSession details;g_render_snapshot_active=true;
        if(mode)level.api.create_texture_dds=ThrowUpload;else f->cancel_at=1;
        bool threw=false;int32_t result=KICH_R4_OK;
        try{result=kich_r4_texture::Preflight(frame,level,batch,input,details,false,NULL,true);}catch(const std::runtime_error&){threw=true;}
        g_render_snapshot_active=false;
        Require(mode?threw:result==KICH_R4_FRAME_SUSPENDED,"probe preserves original exception/cancellation outcome");
        Require(level.preflight_probe.measured && !level.texture_probe && !level.texture_measure_snapshot,"failure restores diagnostic sink and snapshot state");
        Stop(level,frame);
    }
}

}
int main(){try{(void)&Scenario;SingleRead();Capacity();EpochAndBudget();ManySmallSources();LevelReset();Cancellation();UploadFailures();Exceptions();PreflightLoading();ProbeGating();ProbeFailureScopes();printf("E1.6 bounded prepared DDS/loading transactions: %d checks passed\n",checks);return 0;}
    catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
