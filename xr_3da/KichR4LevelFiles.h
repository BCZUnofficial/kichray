#ifndef KICH_R4_LEVEL_FILES_H
#define KICH_R4_LEVEL_FILES_H
#include <stdint.h>
/* EXE-only borrowed filesystem seam. It never crosses the R4 DLL boundary.
 * open returns a view valid until close. The engine adapter owns its IReader.
 */
struct KichR4FileView {const unsigned char* data;uint32_t bytes;void* owner;};
struct KichR4LevelFiles {
    int32_t (*open)(const char* alias,const char* relative,KichR4FileView*);
    void (*close)(KichR4FileView*);
};
#endif
