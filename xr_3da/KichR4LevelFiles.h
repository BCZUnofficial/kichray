#ifndef KICH_R4_LEVEL_FILES_H
#define KICH_R4_LEVEL_FILES_H
#include <stdint.h>
/* EXE-only borrowed filesystem seam. It never crosses the R4 DLL boundary.
 * open returns a view valid until close. The engine adapter owns its IReader.
 */
struct KichR4FileView {const unsigned char* data;uint32_t bytes;void* owner;};
// P0-S11 PERF01: where a file's bytes lie on disk, found on the main thread by
// a lookup only (no read). A loose file is the whole of path; an archived one
// is stored bytes at offset in the archive path, LZO-compressed when
// compressed (size is then the unpacked length).
struct KichR4FileLocation {char path[520];uint64_t offset;uint32_t stored,size,compressed;};
struct KichR4LevelFiles {
    typedef int32_t (*Open)(const char* alias,const char* relative,KichR4FileView*);
    typedef void (*Close)(KichR4FileView*);
    typedef uint32_t (*Sector)(const float* position);
    typedef int32_t (*Locate)(const char* alias,const char* relative,KichR4FileLocation*);
    Open open;Close close;
    // Native engine only: authoritative CFORM/portal query; UINT32_MAX means
    // unknown. Test viewers and non-native adapters keep the null fallback.
    Sector sector;
    // Native engine only (P0-S11); null keeps every read synchronous.
    Locate locate;
    KichR4LevelFiles(Open o=0,Close c=0,Sector s=0,Locate l=0):open(o),close(c),sector(s),locate(l){}
};
#endif
