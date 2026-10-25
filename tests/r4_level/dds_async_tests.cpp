// P0-S11 PERF01: world DDS reads leave the frame (KichR4TextureIO.h). Real
// temporary files, the production registry and a manually driven loader.
#include "recording.h"
#include "../../xr_3da/KichR4TexturePreflight.h"
#include <chrono>
namespace {
std::string Folder(){char temp[MAX_PATH]={};GetTempPathA(MAX_PATH,temp);const std::string folder=std::string(temp)+"kich_dds_async\\";CreateDirectoryA(folder.c_str(),NULL);return folder;}
std::string Write(const std::string& name,const fixture::Bytes& bytes){const std::string path=Folder()+name;
    FILE* out=NULL;fopen_s(&out,path.c_str(),"wb");Require(out!=NULL,"temporary file");fwrite(bytes.data(),1,bytes.size(),out);fclose(out);return path;}
std::map<std::string,std::string> written;int locates=0;bool broken_location=false;
// The engine lookup: the fixture file, written once to a real temporary file.
int32_t Locate(const char* alias,const char* relative,KichR4FileLocation* out){memset(out,0,sizeof(*out));++locates;
    const std::string key=std::string(alias)+"/"+relative;const auto it=f->files.find(key);if(it==f->files.end())return KICH_R4_E_HANDLE;
    std::string& path=written[key];if(path.empty()){std::string flat=relative;for(char& c:flat)if(c=='\\' || c=='/')c='_';path=Write(flat,it->second);}
    strcpy_s(out->path,sizeof(out->path),broken_location?(Folder()+"missing.dds").c_str():path.c_str());
    out->stored=out->size=static_cast<uint32_t>(it->second.size());return 0;}
// Fake LZO: a 4-byte tag, then the plain bytes.
uint32_t Unpack(void* dst,uint32_t dst_len,const void* src,uint32_t src_len){if(src_len!=dst_len+4 || memcmp(src,"LZO!",4))return 0;memcpy(dst,static_cast<const char*>(src)+4,dst_len);return dst_len;}
void Setup(KichR4LevelSession& level,KichR4FrameSession& frame,kich_r4_texture_io::Loader& loader){Configure(level,frame);level.files.locate=Locate;level.texture_io=&loader;
    Require(frame.Start(reinterpret_cast<void*>(1),640,480)==0 && level.Placeholder(frame,false,&level.white)==0 && level.Placeholder(frame,true,&level.checker)==0,"async setup");
    level.TransparentPlaceholder(frame);Require(level.transparent!=0,"transparent stand-in");}
void Stop(KichR4LevelSession& level,KichR4FrameSession& frame){KichR4GeometryReport report={};
    Require(level.bridge.Stop(frame,&report)==0 && f->objects.empty() && f->opens==f->closes && !f->leases,"GPU/reader ownership balances");}
void Next(KichR4LevelSession& level){level.texture_registry.Begin();}
void Read(){
    fixture::Bytes plain=fixture::Dds();const std::string loose=Write("read_plain.dds",plain);
    KichR4FileLocation where={};strcpy_s(where.path,sizeof(where.path),loose.c_str());where.stored=where.size=uint32_t(plain.size());
    std::vector<unsigned char> out;Require(kich_r4_texture_io::Read(where,Unpack,out)==0 && out==plain,"loose file read whole");
    fixture::Bytes archive(4096,0xcd);archive.insert(archive.end(),{'L','Z','O','!'});archive.insert(archive.end(),plain.begin(),plain.end());archive.resize(archive.size()+100,0xee);
    strcpy_s(where.path,sizeof(where.path),Write("read_archive.db",archive).c_str());where.offset=4096;where.stored=uint32_t(plain.size()+4);where.size=uint32_t(plain.size());where.compressed=1;
    Require(kich_r4_texture_io::Read(where,Unpack,out)==0 && out==plain,"archived compressed record read at its offset and unpacked");
    Require(kich_r4_texture_io::Read(where,NULL,out)!=0,"compressed record needs the unpacker");
    where.offset=uint64_t(archive.size())-10;Require(kich_r4_texture_io::Read(where,Unpack,out)!=0,"short record fails, never returns partial bytes");
    where.compressed=0;where.offset=4096;where.stored=10;Require(kich_r4_texture_io::Read(where,Unpack,out)!=0,"uncompressed record must be stored at full size");
    strcpy_s(where.path,sizeof(where.path),(Folder()+"none.dds").c_str());where.stored=where.size=10;Require(kich_r4_texture_io::Read(where,Unpack,out)!=0,"missing file fails");
}
void LoaderQueue(){using namespace kich_r4_texture_io;
    const fixture::Bytes plain=fixture::Dds();KichR4FileLocation where={};strcpy_s(where.path,sizeof(where.path),Write("queue.dds",plain).c_str());where.stored=where.size=uint32_t(plain.size());
    Loader loader(Unpack,plain.size(),false);
    Require(loader.Request("normal",where,kich_r4_texture::NORMAL,1) && loader.Request("base",where,kich_r4_texture::BASE,1) && !loader.Request("base",where,kich_r4_texture::BASE,1),"one request per key");
    Require(loader.Query("base")==Queued && loader.Query("other")==Absent,"queued state");
    Require(loader.RunOne() && loader.Query("base")==Ready && loader.Query("normal")==Queued,"base before normal maps (role priority)");
    Require(!loader.RunOne(),"ready bytes at the limit hold further reads");
    std::vector<unsigned char> bytes;int32_t status=-1;Require(loader.Take("base",bytes,status) && !status && bytes==plain && loader.Query("base")==Absent,"take moves the bytes out");
    Require(loader.RunOne() && loader.Query("normal")==Ready,"taking frees room for the next read");
    loader.Expire(700,600);Require(loader.Query("normal")==Absent && loader.Snapshot().expired==1,"an untaken read expires");
    Require(loader.Request("late",where,kich_r4_texture::BASE,1),"request after expiry");loader.Clear();
    Require(loader.Query("late")==Absent && !loader.RunOne() && loader.Snapshot().cleared==1,"clear drops the queue");
    {Loader threaded(Unpack);Require(threaded.Request("t",where,kich_r4_texture::BASE,1),"threaded request");
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(threaded.Query("t")!=Ready && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Require(threaded.Query("t")==Ready && threaded.Snapshot().completed==1,"worker thread reads in the background");}
}
void WorldTexture(){Fixture fixture;f=&fixture;written.clear();kich_r4_texture_io::Loader loader(Unpack,kich_r4_texture_io::DefaultLimit,false);
    KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame,loader);const int calls=f->texture_calls;
    level.NeedTexture("base",kich_r4_texture::BASE);
    Require(f->opens==0 && loader.Query("base")==kich_r4_texture_io::Queued && level.texture_registry.entries["base"].pending==level.texture_registry.epoch,"cold world texture is queued, never opened in the frame");
    Require(level.CommitTexturePlan(frame)==0 && f->texture_calls==calls && f->opens==0,"pending texture is not planned or uploaded");
    Require(level.Texture(frame,"base")==level.white && f->opens==0 && level.async_window.transitional==1,"its draw gets the neutral stand-in");
    Require(loader.RunOne(),"worker reads");Next(level);
    level.NeedTexture("base",kich_r4_texture::BASE);
    Require(!level.texture_registry.entries["base"].levels.empty() && level.prepared_textures.count("base") && level.async_window.staged==1,"next frame parses the staged bytes");
    Require(level.CommitTexturePlan(frame)==0 && f->texture_calls==calls+1 && f->opens==0,"and uploads them without a synchronous read");
    const KichR4Handle handle=level.Texture(frame,"base");Require(handle!=level.white && handle!=level.checker,"resident texture resolves");
    Next(level);level.NeedTexture("base",kich_r4_texture::BASE);Require(loader.Query("base")==kich_r4_texture_io::Absent && level.texture_registry.entries["base"].pending==0,"resident texture needs no read");
    Stop(level,frame);
}
void Roles(){Fixture fixture;f=&fixture;written.clear();kich_r4_texture_io::Loader loader(Unpack,kich_r4_texture_io::DefaultLimit,false);
    KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame,loader);
    f->files["$game_textures$/fx.dds"]=fixture::Dds();f->files["$game_textures$/bump.dds"]=fixture::Dds();f->files["$game_textures$/hud.dds"]=fixture::Dds();
    level.NeedTexture("hud",kich_r4_texture::HUD);Require(f->opens==1 && loader.Query("hud")==kich_r4_texture_io::Absent,"HUD stays synchronous");
    level.NeedTexture("fx",kich_r4_texture::EFFECT);level.NeedTexture("bump",kich_r4_texture::NORMAL);level.NeedTexture("leaf",kich_r4_texture::SHADOW_ALPHA);
    Require(level.CommitTexturePlan(frame)==0,"plan");
    Require(level.Texture(frame,"fx")==level.transparent && level.Texture(frame,"leaf")==level.transparent && level.Texture(frame,"bump")==level.checker,"effects/shadow alpha invisible, optional maps read as absent");
    level.NeedTexture("base",kich_r4_texture::BASE);const int opens=f->opens;level.NeedTexture("base",kich_r4_texture::UI);
    Require(f->opens==opens+1 && level.texture_registry.entries["base"].pending==0,"a UI demand in the same frame reverts to the synchronous read");
    Next(level);level.texture_sync=true;level.NeedTexture("fresh",kich_r4_texture::BASE);f->files["$game_textures$/fresh.dds"]=fixture::Dds();
    level.NeedTexture("fresh2",kich_r4_texture::BASE);level.texture_sync=false;
    Require(loader.Query("fresh")==kich_r4_texture_io::Absent && loader.Query("fresh2")==kich_r4_texture_io::Absent,"precache/loading frames read synchronously");
    Stop(level,frame);
}
void Failures(){Fixture fixture;f=&fixture;written.clear();kich_r4_texture_io::Loader loader(Unpack,kich_r4_texture_io::DefaultLimit,false);
    KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame,loader);
    broken_location=true;level.NeedTexture("base",kich_r4_texture::BASE);broken_location=false;Require(loader.RunOne() && loader.Query("base")==kich_r4_texture_io::Failed,"stale location fails in the worker");
    Next(level);level.NeedTexture("base",kich_r4_texture::BASE);
    Require(f->opens==1 && !level.texture_registry.entries["base"].levels.empty() && level.async_window.failures==1,"a failed background read falls back to the established read once");
    level.NeedTexture("absent",kich_r4_texture::BASE);
    Require(loader.Query("absent")==kich_r4_texture_io::Absent && level.texture_registry.entries["absent"].reason==kich_r4_texture::SourceUnavailable,"no location: the synchronous path records the missing source");
    Require(level.CommitTexturePlan(frame)==0 && level.Texture(frame,"base")!=level.checker,"fallback upload");Stop(level,frame);
}
void Eviction(){Fixture fixture;f=&fixture;written.clear();kich_r4_texture_io::Loader loader(Unpack,kich_r4_texture_io::DefaultLimit,false);
    KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame,loader);level.bridge.resources.release_resource=[](KichR4Handle,KichR4Handle h)->int32_t{f->objects.erase(h);return 0;};
    level.NeedTexture("base",kich_r4_texture::BASE);loader.RunOne();Next(level);level.NeedTexture("base",kich_r4_texture::BASE);level.CommitTexturePlan(frame);
    kich_r4_texture::Entry& e=level.texture_registry.entries["base"];Require(e.handle!=0,"resident");
    level.bridge.resources.release_resource(frame.device,e.handle);level.texture_registry.bytes-=e.bytes;e.handle=e.bytes=0;e.reason=kich_r4_texture::Evicted;
    Next(level);const int opens=f->opens;level.NeedTexture("base",kich_r4_texture::BASE);
    Require(f->opens==opens && loader.Query("base")==kich_r4_texture_io::Queued && e.pending==level.texture_registry.epoch,"an evicted texture is re-read in the background, metadata kept");
    Require(level.CommitTexturePlan(frame)==0 && level.Texture(frame,"base")==level.white && f->opens==opens,"and is not reloaded synchronously meanwhile");
    loader.RunOne();Next(level);level.NeedTexture("base",kich_r4_texture::BASE);Require(level.CommitTexturePlan(frame)==0 && e.handle && f->opens==opens,"re-read upload");
    level.NeedTexture("leaf",kich_r4_texture::BASE);
    level.texture_override=[](void*,const std::string&,bool& handled)->KichR4Handle{handled=false;return 0;};
    Require(level.Start(frame,"l01_escape")==0 && loader.Query("leaf")==kich_r4_texture_io::Absent,"a new level drops the previous queue");Stop(level,frame);
}
void PreflightFrame(){Fixture fixture;f=&fixture;written.clear();kich_r4_texture_io::Loader loader(Unpack,kich_r4_texture_io::DefaultLimit,false);
    KichR4LevelSession level;KichR4FrameSession frame={};Setup(level,frame,loader);
    kich_r4_capture::Batch batch;kich_r4_capture::Style style;batch.commands.push_back({0,3,2,"base",style});batch.commands.push_back({0,3,0,"leaf",style});
    KichR4GameFrame input={};KichR4DetailSession details;
    Require(kich_r4_texture::Preflight(frame,level,batch,input,details,false,NULL,true)==0 && f->opens==1 && loader.Query("base")==kich_r4_texture_io::Absent,"menu/precache preflight reads UI synchronously");
    Require(!level.texture_sync,"sync scope restored");Stop(level,frame);
}
}
int main(){try{(void)&Scenario;Read();LoaderQueue();WorldTexture();Roles();Failures();Eviction();PreflightFrame();
    printf("P0-S11 background DDS reads: %d checks passed\n",checks);return 0;}
    catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
