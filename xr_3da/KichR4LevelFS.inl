/* Included by EngineAPI.cpp after the engine PCH, and by the filesystem seam
 * tests with recording IReader/FS services. Does not change filesystem aliases. */
#include "KichR4LevelFiles.h"
namespace {
int32_t KichR4_OpenLevelFile(const char* alias,const char* relative,KichR4FileView* out)
{
    if(!alias || !relative || !out)return static_cast<int32_t>(0x80070057u);
    out->data=0;out->bytes=0;out->owner=0;
    if(!FS.path_exist(alias) || !FS.exist(alias,relative))return static_cast<int32_t>(0x80070002u);
    IReader* reader=FS.r_open(alias,relative);
    if(!reader)return static_cast<int32_t>(0x80070002u);
    const int length=reader->length();
    if(length<=0 || static_cast<uint32_t>(length)>256u*1024u*1024u){FS.r_close(reader);return static_cast<int32_t>(0x80070057u);}
    out->data=static_cast<const unsigned char*>(reader->pointer());out->bytes=static_cast<uint32_t>(length);out->owner=reader;
    return 0;
}
void KichR4_CloseLevelFile(KichR4FileView* view)
{
    if(!view)return;
    IReader* reader=static_cast<IReader*>(view->owner);
    if(reader)FS.r_close(reader);
    view->data=0;view->bytes=0;view->owner=0;
}
// P0-S11: the lookup r_open performs (lower-case name, alias path, file
// record), without opening, mapping or decompressing anything.
int32_t KichR4_LocateFile(const char* full,const CLocatorAPI::file* f,KichR4FileLocation* out)
{
    if(!f || !full)return static_cast<int32_t>(0x80070002u);
    if(!f->size_real || f->size_real>256u*1024u*1024u)return static_cast<int32_t>(0x80070057u);
    const char* path=f->vfs==0xffffffff?full:xr_fs_archive_path(FS,f->vfs);
    if(!path || strlen(path)>=sizeof(out->path))return static_cast<int32_t>(0x80070002u);
    strcpy_s(out->path,sizeof(out->path),path);
    if(f->vfs==0xffffffff){out->offset=0;out->stored=out->size=f->size_real;out->compressed=0;}
    else{out->offset=f->ptr;out->stored=f->size_compressed;out->size=f->size_real;out->compressed=f->size_real!=f->size_compressed?1u:0u;}
    return 0;
}
int32_t KichR4_LocateLevelFile(const char* alias,const char* relative,KichR4FileLocation* out)
{
    if(!alias || !relative || !out)return static_cast<int32_t>(0x80070057u);
    memset(out,0,sizeof(*out));
    if(!FS.path_exist(alias))return static_cast<int32_t>(0x80070002u);
    string_path name,full;strcpy_s(name,sizeof(name),relative);xr_strlwr(name);
    return KichR4_LocateFile(full,FS.exist(full,alias,name),out);
}
const KichR4LevelFiles kich_r4_level_files={KichR4_OpenLevelFile,KichR4_CloseLevelFile,0,KichR4_LocateLevelFile};
}
