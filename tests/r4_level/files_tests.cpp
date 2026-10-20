#include "../../xr_3da/KichR4LevelFiles.h"
#include <stdio.h>
#include <string>
#include <stdexcept>
int opened=0,closed=0;
bool exists=true,alias_exists=true;
int length=32;
unsigned char bytes[32]={};
struct IReader {int length()const{return ::length;}void* pointer(){return bytes;}};
struct Files {bool path_exist(const char* alias){return std::string(alias)=="$game_levels$" && alias_exists;}
    bool exist(const char*,const char*){return exists;}
    IReader* r_open(const char*,const char*){++opened;return new IReader();}
    void r_close(IReader*& reader){++closed;delete reader;reader=NULL;}
} FS;
#include "../../xr_3da/KichR4LevelFS.inl"
int main(){KichR4FileView view={};
    if(kich_r4_level_files.open("$game_levels$","l01_escape\\level",&view)!=0 || view.bytes!=32 || view.data!=bytes)return 1;
    kich_r4_level_files.close(&view);kich_r4_level_files.close(&view);
    if(opened!=1 || closed!=1 || view.owner || view.data || view.bytes)return 2;
    exists=false;if(kich_r4_level_files.open("$game_levels$","missing",&view)>=0 || opened!=1)return 3;
    exists=true;alias_exists=false;if(kich_r4_level_files.open("$game_levels$","level",&view)>=0 || opened!=1)return 4;
    alias_exists=true;length=-1;if(kich_r4_level_files.open("$game_levels$","level",&view)>=0 || opened!=2 || closed!=2)return 5;
    puts("B4.2 actual engine filesystem adapter: open/missing/close/invalid size passed");return 0;
}
