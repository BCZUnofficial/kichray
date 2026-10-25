// P0-S11 PERF02: the cheaper capture paths decide exactly as before.
//  - native model deduplication against the former string key (oracle copy);
//  - centre/extent bounds contain every transformed corner and stay tight;
//  - the dense/sparse stream vertex remap;
//  - the queue stage profile gating.
#include "KichR4CaptureData.h"
#include "KichR4QueueProfile.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace kich_r4_capture;
namespace {
int checks=0;
void Require(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
uint32_t seed=12345;uint32_t Next(){seed=seed*1664525u+1013904223u;return seed>>8;}
float Uniform(float lo,float hi){return lo+(hi-lo)*float(Next()%100000)/100000.f;}
// The key string Batch::NativeModel built before P0-S11, verbatim in content.
std::string FormerKey(const void* identity,const std::shared_ptr<Mesh>& mesh,const Matrix& world,const Matrix& vp,const Matrix* bones,uint32_t count,uint32_t first,uint32_t tris,
    const Style& style,uint32_t skin_mode,const char* shader,const char* texture,const char* secondary,uint32_t effect,uint32_t shader_element,uint32_t shader_pass,
    const kich_r4_material::Descriptor* material,bool light_valid,float object_hemi,float object_sun){
    const uint32_t layer=style.max_z<.1f?1:0;const std::string s=shader?shader:mesh->shader,t=texture?texture:mesh->texture,sec=secondary?secondary:"";
    std::string key(reinterpret_cast<const char*>(&identity),sizeof(identity));const Mesh* source=mesh.get();key.append(reinterpret_cast<const char*>(&source),sizeof(source));
    key.append(reinterpret_cast<const char*>(&world),sizeof(world));key.append(reinterpret_cast<const char*>(&vp),sizeof(vp));
    const uint32_t words[]={first,tris,count,skin_mode,layer,effect,shader_element,shader_pass,style.blend,style.src,style.dst,style.clamp,style.cull,style.depth,style.write,style.alpha_func,style.shadow_alpha_func};
    key.append(reinterpret_cast<const char*>(words),sizeof(words));key.append(reinterpret_cast<const char*>(&style.alpha_ref),sizeof(style.alpha_ref));
    key.append(reinterpret_cast<const char*>(&style.shadow_alpha_ref),sizeof(style.shadow_alpha_ref));
    const float emission[]={style.emissive_gain,style.emissive_specular};key.append(reinterpret_cast<const char*>(emission),sizeof(emission));
    key.append(reinterpret_cast<const char*>(&style.surface),sizeof(style.surface));
    const float exposure[]={light_valid?1.f:0.f,object_hemi,object_sun};key.append(reinterpret_cast<const char*>(exposure),sizeof(exposure));
    const float depth_range[]={style.min_z,style.max_z};key.append(reinterpret_cast<const char*>(depth_range),sizeof(depth_range));
    key.append(reinterpret_cast<const char*>(&style.scissor),sizeof(style.scissor));if(style.scissor)key.append(reinterpret_cast<const char*>(style.rect),sizeof(style.rect));
    if(mesh->skinned)key.append(reinterpret_cast<const char*>(bones),mesh->bones*sizeof(Matrix));
    key.append(s);key.push_back('\0');key.append(t);key.push_back('\0');key.append(sec);
    if(material){const uint64_t material_key=kich_r4_material::Key(*material);key.push_back('\0');key.append(reinterpret_cast<const char*>(&material_key),sizeof(material_key));}
    return key;}
std::shared_ptr<Mesh> Source(bool skinned){std::shared_ptr<Mesh> m(new Mesh());m->skinned=skinned;m->two_weights=false;m->bones=skinned?2:0;m->vertices.resize(3);m->indices={0,1,2,0,2,1};
    m->shader="models\\model";m->texture="base";
    for(unsigned i=0;i<3;++i){auto& v=m->vertices[i];v={};v.p[0]=i==1?.5f:0;v.p[1]=i==2?.5f:0;v.p[2]=.5f;v.normal=PackNormal(0,0,1);v.bone[1]=skinned?1:0;}
    return m;}
void Deduplication(){
    const std::shared_ptr<Mesh> meshes[2]={Source(true),Source(false)};
    Matrix worlds[3]={kich_r4_scene::Identity(),kich_r4_scene::Identity(),kich_r4_scene::Identity()};worlds[1].m[12]=4;worlds[2].m[12]=-0.f; // -0 differs bytewise, as before
    Matrix vps[2]={kich_r4_scene::Identity(),kich_r4_scene::Identity()};vps[1].m[0]=1.5f;
    Matrix poses[3][2];for(auto& p:poses)p[0]=p[1]=kich_r4_scene::Identity();poses[1][1].m[13]=2;poses[2][1].m[13]=-0.f;
    kich_r4_material::Descriptor a;a.semantic=kich_r4_material::MATERIAL_OPAQUE;a.shader="models\\model";a.base="act\\a";a.bump="act\\a_bump";
    kich_r4_material::Descriptor a_copy=a,b=a;b.base="act\\b";
    const kich_r4_material::Descriptor* materials[4]={NULL,&a,&a_copy,&b}; // a_copy: equal content at another address
    const char* shaders[2]={NULL,"models\\model"};const char* textures[3]={NULL,"base","other"};const char* secondaries[2]={NULL,""};
    for(unsigned run=0;run<40;++run){Batch batch;batch.Begin(640,480,run+1);std::set<std::string> former;unsigned kept=0;
        for(unsigned call=0;call<400;++call){
            const void* identity=reinterpret_cast<const void*>(uintptr_t(1+Next()%2));const std::shared_ptr<Mesh>& mesh=meshes[Next()%2];
            const Matrix& world=worlds[Next()%3];const Matrix& vp=vps[Next()%2];const Matrix* bones=poses[Next()%3];
            Style style;const unsigned variant=Next()%6;
            if(variant==1)style.surface.color_mask=7;
            if(variant==2){style.scissor=false;style.rect[0]=Next()%5;} // rect ignored without scissor
            if(variant==3){style.scissor=true;style.rect[0]=Next()%2;}
            if(variant==4){style.min_z=0;style.max_z=.02f;}
            if(variant==5)style.alpha_ref=Next()%2?.5f:.25f;
            const uint32_t first=Next()%2?0:3,effect=Next()%3==0?5:0,element=Next()%2,pass=Next()%2;
            const char* shader=shaders[Next()%2];const char* texture=textures[Next()%3];const char* secondary=secondaries[Next()%2];
            const kich_r4_material::Descriptor* material=materials[Next()%4];const bool light=Next()%2!=0;const float hemi=light?.5f:1.f;
            const bool expected=former.insert(FormerKey(identity,mesh,world,vp,bones,3,first,1,style,KICH_R4_SKIN_HARDWARE,shader,texture,secondary,effect,element,pass,material,light,hemi,1.f)).second;
            const size_t before=batch.native_models.size();
            batch.NativeModel(identity,mesh,world,vp,bones,2,3,first,1,style,KICH_R4_SKIN_HARDWARE,"m",shader,texture,secondary,effect,element,pass,NULL,material,light,hemi,1.f);
            const bool added=batch.native_models.size()==before+1;Require(added==expected,"every call is kept or merged exactly as the former key string decided");
            if(added){++kept;const NativeModelCommand& c=batch.native_models.back();
                Require(c.source_style.max_z==style.max_z && c.has_material==(material!=NULL) && c.visual_id==uint64_t(reinterpret_cast<uintptr_t>(identity)),"stored identity fields");}}
        Require(kept==former.size() && batch.native_model_duplicates==400-kept,"duplicate count");
        Require(batch.models+batch.hands==kept,"layer counters");}
    Batch batch;batch.Begin(1,1,1);batch.NativeModel(reinterpret_cast<void*>(1),meshes[1],worlds[0],vps[0],NULL,0,3,0,1,Style(),KICH_R4_SKIN_HARDWARE,"m");
    batch.Begin(1,1,2);Require(batch.native_models.empty() && batch.native_model_heads.empty() && batch.native_model_next.empty() && !batch.native_model_duplicates,"a new frame forgets the buckets");
    batch.NativeModel(reinterpret_cast<void*>(1),meshes[1],worlds[0],vps[0],NULL,0,3,0,1,Style(),KICH_R4_SKIN_HARDWARE,"m");Require(batch.native_models.size()==1,"and accepts the same draw again");
}
void BoundsMath(){
    for(unsigned trial=0;trial<20000;++trial){Bounds source;for(unsigned a=0;a<3;++a){const float x=Uniform(-50,50),y=x+Uniform(0,5);source.min[a]=x;source.max[a]=y;}source.valid=true;
        if(trial%7==0)std::swap(source.min[0],source.max[0]); // inverted axis, as corners always allowed
        Matrix m=kich_r4_scene::Identity();for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)m.m[r*4+c]=Uniform(-2,2);
        for(unsigned c=0;c<3;++c)m.m[12+c]=Uniform(-1000,1000);if(trial%11==0){m.m[12]=1.e6f;} // far translation
        Bounds fast;Require(AddTransformedBounds(fast,source,m) && fast.valid,"affine box transforms");
        double lo[3]={1e30,1e30,1e30},hi[3]={-1e30,-1e30,-1e30},terms[3]={0,0,0}; // terms: the float rounding scale of any method
        for(unsigned corner=0;corner<8;++corner){double p[3];for(unsigned i=0;i<3;++i)p[i]=(corner&(1u<<i))?source.max[i]:source.min[i];
            for(unsigned j=0;j<3;++j){const double q=p[0]*m.m[j]+p[1]*m.m[4+j]+p[2]*m.m[8+j]+m.m[12+j];lo[j]=std::min(lo[j],q);hi[j]=std::max(hi[j],q);
                terms[j]=std::max(terms[j],std::fabs(p[0]*m.m[j])+std::fabs(p[1]*m.m[4+j])+std::fabs(p[2]*m.m[8+j])+std::fabs(m.m[12+j]));}}
        for(unsigned j=0;j<3;++j){Require(fast.min[j]<=lo[j] && fast.max[j]>=hi[j],"centre/extent bounds contain every exact transformed corner");
            const double scale=terms[j]+1;Require(lo[j]-fast.min[j]<=scale*4e-6 && fast.max[j]-hi[j]<=scale*4e-6,"and are no looser than a few ppm of the term magnitude");}}
    Bounds source;source.valid=true;for(unsigned a=0;a<3;++a){source.min[a]=-1;source.max[a]=1;}
    Matrix projective=kich_r4_scene::Identity();projective.m[11]=1;Bounds out;Require(!AddTransformedBounds(out,source,projective),"non-affine matrix still refused");
    Matrix huge=kich_r4_scene::Identity();huge.m[12]=3.e20f;Require(!AddTransformedBounds(out,source,huge),"out-of-range result still refused");
    Bounds empty;Require(AddTransformedBounds(out,empty,huge) && !out.valid,"invalid source adds nothing");
}
void Remap(){VertexRemap remap;std::map<uint32_t,uint32_t> ordered;
    remap.Begin(100,110,6);Require(remap.use_dense && remap.Find(105)==UINT32_MAX,"compact range uses the dense table");
    remap.Set(105,7);remap.Set(100,3);Require(remap.Find(105)==7 && remap.Find(100)==3 && remap.Find(110)==UINT32_MAX,"dense lookup");
    remap.Ordered(ordered);Require(ordered.size()==2 && ordered.begin()->first==100 && ordered.rbegin()->second==7,"ascending reused sources");
    remap.Begin(0,4000000,6);Require(!remap.use_dense,"a sparse range uses the hash map");remap.Set(4000000,1);remap.Set(2,9);
    Require(remap.Find(4000000)==1 && remap.Find(2)==9 && remap.Find(3)==UINT32_MAX,"sparse lookup");remap.Ordered(ordered);Require(ordered.size()==2 && ordered.begin()->first==2,"sparse ordered");
    remap.Begin(5,5,1);Require(remap.use_dense && remap.dense.size()==1 && remap.Find(5)==UINT32_MAX,"next draw starts empty");
}
void QueueProfile(){using namespace kich_r4_queue;kich_r4_queue::Profile& p=Global();p.Reset();
    p.Begin(false);p.Count(ModelCalls,5);p.Add(Submit,3);{Scope s(Submit);}p.Finish();Require(!p.frames && !p.count_sum[ModelCalls] && p.sum[Submit]==0,"unmeasured frames cost and record nothing");
    p.Begin(true);p.Count(ModelCalls,5);p.Add(Submit,3);p.Add(Submit,1);p.Finish();p.Finish();
    p.Begin(true);p.Add(Submit,2);p.Finish();
    Require(p.frames==2 && p.sum[Submit]==6 && p.peak[Submit]==4 && p.count_sum[ModelCalls]==5,"window sums, peaks and counts; Finish is idempotent");
    p.Reset();Require(!p.frames && p.sum[Submit]==0,"report resets the window");
    Require(!strcmp(StageName(ModelCapture),"model_capture") && !strcmp(CounterName(StreamIndices),"stream_indices"),"names");
}
void Timing(){
    std::shared_ptr<Mesh> mesh(new Mesh());mesh->skinned=true;mesh->bones=60;mesh->vertices.resize(2000);
    for(auto& v:mesh->vertices){v={};v.p[2]=.5f;v.normal=PackNormal(0,0,1);v.bone[0]=1;v.bone[1]=2;}
    for(unsigned i=0;i<3000;++i){mesh->indices.push_back(uint16_t(i%2000));mesh->indices.push_back(uint16_t((i+1)%2000));mesh->indices.push_back(uint16_t((i+2)%2000));}
    mesh->bone_bounds.resize(60);for(auto& b:mesh->bone_bounds){b.valid=true;for(unsigned a=0;a<3;++a){b.min[a]=-1;b.max[a]=1;}}mesh->FreezeRanges();
    kich_r4_material::Descriptor material;material.semantic=kich_r4_material::MATERIAL_OPAQUE;material.shader="models\\model_pn_bump_stalker";material.base="act\\act_stalker_novice_1";material.bump="act\\act_stalker_novice_1_bump";
    Matrix bones[60];for(auto& b:bones)b=kich_r4_scene::Identity();Matrix world=kich_r4_scene::Identity(),vp=world;Style style;Batch batch;double best=1e9;
    for(unsigned run=0;run<10;++run){batch.Begin(1920,1080,run+1);const auto t0=std::chrono::steady_clock::now();
        for(unsigned i=0;i<1500;++i){world.m[12]=float(i);for(unsigned k=0;k<(i%3?1u:2u);++k)
            batch.NativeModel(reinterpret_cast<void*>(uintptr_t(i+1)),mesh,world,vp,bones,60,2000,0,3000,style,KICH_R4_SKIN_HARDWARE,"npc",NULL,NULL,NULL,0,0,0,NULL,&material);}
        best=std::min(best,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count());}
    Require(batch.native_models.size()==1500 && batch.native_model_duplicates==500,"timing scene");
    printf("  2000 native model calls (1500 unique, 60 bones, material): %.3f ms best of 10\n",best);
}
}
int main(){try{Deduplication();BoundsMath();Remap();QueueProfile();Timing();printf("P0-S11 capture cost paths: %d checks passed\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
