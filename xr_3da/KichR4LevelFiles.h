#ifndef KICH_R4_LEVEL_FILES_H
#define KICH_R4_LEVEL_FILES_H
#include <stdint.h>
/* EXE-only borrowed filesystem seam. It never crosses the R4 DLL boundary.
 * open returns a view valid until close. The engine adapter owns its IReader.
 */
struct KichR4FileView {const unsigned char* data;uint32_t bytes;void* owner;};
struct KichR4LevelFiles {
    typedef int32_t (*Open)(const char* alias,const char* relative,KichR4FileView*);
    typedef void (*Close)(KichR4FileView*);
    typedef uint32_t (*Sector)(const float* position);
    Open open;Close close;
    // Native engine only: authoritative CFORM/portal query; UINT32_MAX means
    // unknown. Test viewers and non-native adapters keep the null fallback.
    Sector sector;
    KichR4LevelFiles(Open o=0,Close c=0,Sector s=0):open(o),close(c),sector(s){}
};
#endif
