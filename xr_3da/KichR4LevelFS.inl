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
const KichR4LevelFiles kich_r4_level_files={KichR4_OpenLevelFile,KichR4_CloseLevelFile};
}
