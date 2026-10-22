/* One-shot diagnostic recorder. No engine, graphics API, GPU wait or worker. */
#include "RenderSnapshot.h"
#if !defined(_EDITOR)
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <chrono>
#include <string>
#include <map>
#include <sstream>
#include <iomanip>
#if defined(_WIN32)
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif
bool g_render_snapshot_active=false;
bool g_render_snapshot_console=false;
namespace {
const size_t max_bytes=8u*1024u*1024u,max_events=32768;
unsigned pending=0;
bool incomplete=false,overflowed=false;
const char* failure=NULL;
uint64_t draws=0,triangles=0,events=0;
RenderSnapshotInfo info={};
std::string body,pass,category,resource,material,vertex_shader,pixel_shader,texture_names[8],last_path,skin;
struct Pass {uint64_t calls=0,triangles=0;double cpu_ms=0;};
std::map<std::string,Pass> passes;
std::chrono::steady_clock::time_point frame_start,pass_start;
std::string Clean(const char* text,size_t max=512){std::string out;if(!text)return "UNKNOWN";
    for(size_t i=0;i<max && text[i];++i){const unsigned char c=static_cast<unsigned char>(text[i]);out+=(c<32 || c==127)?' ':char(c);}return out;}
void Add(const std::string& text){if(events>=max_events || text.size()>max_bytes-body.size()){incomplete=overflowed=true;return;}body+=text;++events;}
void ClosePass(){if(!pass.empty())passes[pass].cpu_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pass_start).count();}
FILE* Exclusive(const char* path){
#if defined(_WIN32)
    int fd=-1;const int error=_sopen_s(&fd,path,_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_SH_DENYRW,_S_IREAD|_S_IWRITE);if(error){errno=error;return NULL;}
    FILE* file=_fdopen(fd,"wb");if(!file)_close(fd);return file;
#else
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return NULL;FILE* file=fdopen(fd,"wb");if(!file)close(fd);return file;
#endif
}
}
bool RenderSnapshot_Request(){if(pending>=16)return false;++pending;return true;}
bool RenderSnapshot_Wanted(){return pending!=0 || g_render_snapshot_active;}
void RenderSnapshot_Begin(const RenderSnapshotInfo& input){
    if(g_render_snapshot_active || !pending)return;
    --pending;
    try{failure=NULL;info=input;body.clear();passes.clear();pass.clear();category="UNKNOWN";resource="UNKNOWN";material="UNKNOWN";
        vertex_shader=pixel_shader="UNKNOWN";skin="NOT_INSTRUMENTED";g_render_snapshot_console=false;for(unsigned i=0;i<8;++i)texture_names[i]="UNKNOWN";
        incomplete=overflowed=false;draws=triangles=events=0;frame_start=pass_start=std::chrono::steady_clock::now();g_render_snapshot_active=true;
        RenderSnapshot_Pass("FRAME_SETUP");
    }catch(...){g_render_snapshot_active=false;failure="ERROR: snapshot allocation failure";}
}
void RenderSnapshot_Pass(const char* name){if(!g_render_snapshot_active)return;try{ClosePass();pass=Clean(name,96);pass_start=std::chrono::steady_clock::now();category="UNKNOWN";resource=material="UNKNOWN";}catch(...){incomplete=true;}}
void RenderSnapshot_Context(const char* kind,const char* name,const char* shader){if(!g_render_snapshot_active)return;try{category=Clean(kind,64);resource=Clean(name,256);material=Clean(shader,256);skin="NOT_INSTRUMENTED";}catch(...){incomplete=true;}}
void RenderSnapshot_Shaders(const char* vs,const char* ps){if(!g_render_snapshot_active)return;try{vertex_shader=Clean(vs,96);pixel_shader=Clean(ps,96);}catch(...){incomplete=true;}}
void RenderSnapshot_ShaderStage(bool vertex,const char* name){if(!g_render_snapshot_active)return;try{(vertex?vertex_shader:pixel_shader)=Clean(name,96);}catch(...){incomplete=true;}}
void RenderSnapshot_Texture(uint32_t stage,const char* name){if(!g_render_snapshot_active || stage>=8)return;try{texture_names[stage]=Clean(name,128);}catch(...){incomplete=true;}}
void RenderSnapshot_Skin(const char* mode){if(!g_render_snapshot_active)return;try{skin=Clean(mode,32);}catch(...){incomplete=true;}}
void SNAPSHOT_CALL RenderSnapshot_Draw(const RenderSnapshotDraw* d){
    if(!g_render_snapshot_active || !d || d->struct_size!=sizeof(*d))return;
    try{if(!d->submitted)incomplete=true;++draws;const uint64_t tris=d->topology==4?uint64_t(d->index_count?d->index_count:d->vertex_count)/3*d->instance_count:
        d->topology==5 || d->topology==6?uint64_t((d->index_count?d->index_count:d->vertex_count)>2?(d->index_count?d->index_count:d->vertex_count)-2:0)*d->instance_count:0;
        triangles+=d->submitted?tris:0;Pass& p=passes[pass];p.calls+=d->submitted?1:0;p.triangles+=d->submitted?tris:0;
        std::ostringstream out;out<<"\n[DRAW "<<std::setfill('0')<<std::setw(6)<<draws<<"]\nstatus="<<(d->submitted?"SUBMITTED":"SKIPPED")
            <<"\npass="<<pass<<"\ncategory="<<category<<"\nresource="<<resource<<"\nmaterial="<<material
            <<"\nvertex_shader="<<(d->vertex_shader[0]?Clean(d->vertex_shader,sizeof(d->vertex_shader)):vertex_shader)
            <<"\npixel_shader="<<(d->pixel_shader[0]?Clean(d->pixel_shader,sizeof(d->pixel_shader)):pixel_shader)
            <<"\nvertices="<<(d->vertex_count_known?std::to_string(d->vertex_count):"NOT_AVAILABLE")
            <<"\nvb_capacity_vertices="<<(d->vertex_capacity?std::to_string(d->vertex_capacity):"NOT_AVAILABLE")<<"\nindices="<<d->index_count<<"\ninstances="<<d->instance_count
            <<"\ntriangles_submitted="<<(d->submitted?tris:0)<<"\ntopology_raw="<<d->topology<<"\nstart_index="<<d->start_index<<"\nfirst_instance="<<d->first_instance
            <<"\npipeline="<<d->pipeline<<"\nvb="<<d->vb<<"\nib="<<d->ib<<"\ncb="<<d->cb<<"\ninstance_buffer="<<d->instances
            <<"\nvs_handle="<<d->vs<<"\nps_handle="<<d->ps<<"\nskin_mode="<<(d->skin[0]?Clean(d->skin,sizeof(d->skin)):skin)
            <<"\nraw_states="<<Clean(d->states,sizeof(d->states))<<"\nsamplers="<<Clean(d->samplers,sizeof(d->samplers))<<"\n";
        for(unsigned i=0;i<8;++i)out<<"texture"<<i<<"="<<(d->texture_names[i][0]?Clean(d->texture_names[i],sizeof(d->texture_names[i])):texture_names[i])<<"; handle="<<d->textures[i]<<"\n";
        Add(out.str());
    }catch(...){incomplete=true;}
}
void RenderSnapshot_Note(const char* status,const char* kind,const char* name,const char* reason){if(!g_render_snapshot_active)return;try{
    Add("\n[VISIBILITY_EVENT]\nstatus="+Clean(status,32)+"\ncategory="+Clean(kind,64)+"\nresource="+Clean(name,256)+"\nreason="+Clean(reason,256)+"\n");}catch(...){incomplete=true;}}
void RenderSnapshot_Value(const char* section,const char* key,double value){if(!g_render_snapshot_active)return;try{std::ostringstream out;out<<std::setprecision(12)<<value;RenderSnapshot_Text(section,key,out.str().c_str());}catch(...){incomplete=true;}}
void RenderSnapshot_Text(const char* section,const char* key,const char* value){if(!g_render_snapshot_active)return;try{Add("\n["+Clean(section,64)+"]\n"+Clean(key,96)+"="+Clean(value,2048)+"\n");}catch(...){incomplete=true;}}
const char* RenderSnapshot_LastPath(){return failure?failure:last_path.c_str();}
void RenderSnapshot_End(const char* status){
    if(!g_render_snapshot_active)return;
    g_render_snapshot_active=false;
    g_render_snapshot_console=false;
    try{ClosePass();const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frame_start).count();
        const std::string final_status=incomplete || !status || strcmp(status,"COMPLETE")?"INCOMPLETE":"COMPLETE";
        std::ostringstream out;out<<std::setprecision(10)<<"[SNAPSHOT]\nschema=1\nrenderer="<<Clean(info.renderer,sizeof(info.renderer))<<"\napi="<<Clean(info.api,sizeof(info.api))
            <<"\nframe="<<info.frame<<"\nstatus="<<final_status<<"\ncompletion_reason="<<Clean(status,96)<<"\nbuild="<<Clean(info.build,sizeof(info.build))
            <<"\nsource_sha256="<<Clean(info.source_id,sizeof(info.source_id))<<"\nshaders_sha256="<<Clean(info.shader_id,sizeof(info.shader_id))
            <<"\nadapter="<<Clean(info.adapter,sizeof(info.adapter))<<"\nconsole_visible="<<info.console_visible<<"\ngame_paused="<<info.paused<<"\nhud="<<info.hud
            <<"\n\n[SCENE]\nscene="<<Clean(info.scene,sizeof(info.scene))<<"\nlevel="<<Clean(info.level,sizeof(info.level))<<"\nweather="<<Clean(info.weather,sizeof(info.weather))
            <<"\nengine_time="<<info.time<<"\ngame_time_seconds="<<info.game_time<<"\nwind_strength="<<info.wind<<"\nsun_direction="<<info.sun[0]<<","<<info.sun[1]<<","<<info.sun[2]
            <<"\n\n[CAMERA]\nposition="<<info.position[0]<<","<<info.position[1]<<","<<info.position[2]<<"\ndirection="<<info.direction[0]<<","<<info.direction[1]<<","<<info.direction[2]
            <<"\nfov="<<info.fov<<"\nresolution="<<info.width<<"x"<<info.height<<"\n\n[MEASUREMENT_LIMITS]\nvisible_pixels=NOT_INSTRUMENTED\nvisible_objects=UNKNOWN\nbackground_culling=NOT_INSTRUMENTED\n"
            <<"gpu_timing=NOT_INSTRUMENTED\nprocess_gpu_memory=NOT_AVAILABLE\ntexture_slots_8_15=NOT_INSTRUMENTED\n"
            <<"cpu_timing=INSTRUMENTED_FRAME_INCLUDES_TRACE_OVERHEAD\ncoverage=BACKEND_DRAW_CALLS; direct external API calls NOT_INSTRUMENTED\n";
        out<<body;
        for(const auto& entry:passes)out<<"\n[PASS "<<entry.first<<"]\ndraw_calls="<<entry.second.calls<<"\ntriangles="<<entry.second.triangles<<"\ncpu_ms="<<entry.second.cpu_ms<<"\ngpu_ms=NOT_INSTRUMENTED\n";
        out<<"\n[END]\nstatus="<<final_status<<"\ndraw_events="<<draws<<"\ntriangles_submitted="<<triangles<<"\ncpu_frame_ms="<<ms<<"\nevents="<<events<<"\nbuffer_bytes="<<body.size()<<"\noverflow="<<(overflowed?1:0)<<"\n";
        const std::string report=out.str();time_t now=time(NULL);tm local={};
#if defined(_WIN32)
        localtime_s(&local,&now);
#else
        localtime_r(&now,&local);
#endif
        char stamp[64];strftime(stamp,sizeof(stamp),"snap%d.%m.%Y.%H.%M.%S",&local);
        std::string directory=Clean(info.directory,sizeof(info.directory));if(!directory.empty() && directory.back()!='/' && directory.back()!='\\')directory+='/';
        FILE* file=NULL;std::string path;
        for(unsigned suffix=0;suffix<10000 && !file;++suffix){char tail[32];if(suffix)snprintf(tail,sizeof(tail),"_%02u.log",suffix);else strcpy(tail,".log");path=directory+stamp+tail;file=Exclusive(path.c_str());if(!file && errno!=EEXIST)break;}
        if(!file){failure="ERROR: cannot create snapshot file";std::string().swap(body);passes.clear();return;}
        const size_t written=fwrite(report.data(),1,report.size(),file);const int closed=fclose(file);
        last_path=written==report.size() && closed==0?path:"ERROR: incomplete snapshot write";
        std::string().swap(body);passes.clear();
    }catch(...){failure="ERROR: snapshot allocation/write failure";std::string().swap(body);passes.clear();}
}
#endif
