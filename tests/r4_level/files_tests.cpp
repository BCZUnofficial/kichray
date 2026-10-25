#include "../../xr_3da/KichR4LevelFiles.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <string>
#include <stdexcept>
int opened=0,closed=0;
bool exists=true,alias_exists=true;
int length=32;
unsigned char bytes[32]={};
typedef char string_path[520];
inline void xr_strlwr(char* s){for(;*s;++s)*s=char(tolower(static_cast<unsigned char>(*s)));}
struct IReader {int length()const{return ::length;}void* pointer(){return bytes;}};
struct CLocatorAPI {struct file {const char* name;unsigned vfs,crc,ptr,size_real,size_compressed,modif;};};
CLocatorAPI::file record={"",0xffffffffu,0,0,32,32,0};std::string looked_up;
struct Files {bool path_exist(const char* alias){return std::string(alias)=="$game_levels$" && alias_exists;}
    bool exist(const char*,const char*){return exists;}
    const CLocatorAPI::file* exist(string_path& fn,const char* alias,const char* name){looked_up=name;sprintf_s(fn,sizeof(fn),"c:\\game\\%s",name);(void)alias;return exists?&record:NULL;}
    IReader* r_open(const char*,const char*){++opened;return new IReader();}
    void r_close(IReader*& reader){++closed;delete reader;reader=NULL;}
} FS;
const char* xr_fs_archive_path(const Files&,unsigned vfs){return vfs==2?"c:\\game\\gamedata.db2":NULL;}
#include "../../xr_3da/KichR4LevelFS.inl"
int main(){KichR4FileView view={};
    if(kich_r4_level_files.open("$game_levels$","l01_escape\\level",&view)!=0 || view.bytes!=32 || view.data!=bytes)return 1;
    kich_r4_level_files.close(&view);kich_r4_level_files.close(&view);
    if(opened!=1 || closed!=1 || view.owner || view.data || view.bytes)return 2;
    exists=false;if(kich_r4_level_files.open("$game_levels$","missing",&view)>=0 || opened!=1)return 3;
    exists=true;alias_exists=false;if(kich_r4_level_files.open("$game_levels$","level",&view)>=0 || opened!=1)return 4;
    alias_exists=true;length=-1;if(kich_r4_level_files.open("$game_levels$","level",&view)>=0 || opened!=2 || closed!=2)return 5;
    // P0-S11 locate: a lookup only, lower-case like r_open.
    KichR4FileLocation where={};
    if(!kich_r4_level_files.locate || kich_r4_level_files.locate("$game_levels$","Act\\Wall.DDS",&where)!=0 || looked_up!="act\\wall.dds" ||
        strcmp(where.path,"c:\\game\\act\\wall.dds") || where.offset || where.stored!=32 || where.size!=32 || where.compressed || opened!=2)return 6;
    record.vfs=2;record.ptr=4096;record.size_compressed=20;
    if(kich_r4_level_files.locate("$game_levels$","wall.dds",&where)!=0 || strcmp(where.path,"c:\\game\\gamedata.db2") || where.offset!=4096 || where.stored!=20 || where.size!=32 || !where.compressed)return 7;
    record.vfs=5;if(kich_r4_level_files.locate("$game_levels$","wall.dds",&where)>=0)return 8; // unknown archive
    record.vfs=0xffffffffu;exists=false;if(kich_r4_level_files.locate("$game_levels$","wall.dds",&where)>=0)return 9;
    exists=true;alias_exists=false;if(kich_r4_level_files.locate("$game_levels$","wall.dds",&where)>=0)return 10;
    alias_exists=true;record.size_real=0;if(kich_r4_level_files.locate("$game_levels$","wall.dds",&where)>=0 || opened!=2)return 11;
    puts("B4.2 actual engine filesystem adapter: open/missing/close/invalid size, P0-S11 locate loose/archive/missing passed");return 0;
}
