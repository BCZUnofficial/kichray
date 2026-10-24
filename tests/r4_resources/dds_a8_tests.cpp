// P0-S1: DDPF_ALPHA A8 DDS (R2 textures\sunmask.dds) parses with its mip chain
// and converts to RGBA (0,0,0,a), as D3D9 samples A8.
// MSVC: cl /nologo /EHsc /I xr_3da tests\r4_resources\dds_a8_tests.cpp [then run with an optional .dds path]
#include "KichR4Dds.h"
#include <cstdio>
#include <vector>
static int Fail(const char* what){std::printf("FAIL: %s\n",what);return 1;}
static void Put32(std::vector<unsigned char>& d,unsigned at,uint32_t v){for(unsigned i=0;i<4;++i)d[at+i]=static_cast<unsigned char>(v>>(i*8));}
int main(int argc,char** argv){
    // 4x4 A8 with mips 4x4, 2x2, 1x1 (16+4+1 bytes).
    std::vector<unsigned char> d(128+21,0);
    Put32(d,0,0x20534444);Put32(d,4,124);Put32(d,8,0x2100f);Put32(d,12,4);Put32(d,16,4);Put32(d,20,4);Put32(d,28,3);
    Put32(d,76,32);Put32(d,80,0x2);Put32(d,88,8);Put32(d,104,0xff);Put32(d,108,0x401008);
    for(unsigned i=0;i<21;++i)d[128+i]=static_cast<unsigned char>(i*12);
    kich_r4_dds::Image image;
    if(!kich_r4_dds::Parse(d.data(),static_cast<uint32_t>(d.size()),2048,image))return Fail("A8 rejected");
    if(image.format!=kich_r4_dds::A8 || image.count!=3 || image.mips[1].bytes!=4 || image.mips[2].bytes!=1)return Fail("A8 layout");
    unsigned char rgba[4];kich_r4_dds::ToRGBA(image.format,image.mips[0].data+5,rgba);
    if(rgba[0] || rgba[1] || rgba[2] || rgba[3]!=60)return Fail("A8 must sample as (0,0,0,a)");
    if(kich_r4_dds::UploadBytes(image)!=(16+4+1)*4)return Fail("A8 converted upload bytes");
    std::printf("PASS: synthetic A8 DDS parsed (3 mips) and converted to (0,0,0,a)\n");
    if(argc>1){FILE* f=std::fopen(argv[1],"rb");if(!f)return Fail("cannot open file");
        std::vector<unsigned char> file;unsigned char buffer[65536];size_t n;
        while((n=std::fread(buffer,1,sizeof(buffer),f))>0)file.insert(file.end(),buffer,buffer+n);std::fclose(f);
        if(!kich_r4_dds::Parse(file.data(),static_cast<uint32_t>(file.size()),16384,image))return Fail("real file rejected");
        unsigned lo=255,hi=0;for(uint32_t i=0;i<image.mips[0].bytes;++i){lo=image.mips[0].data[i]<lo?image.mips[0].data[i]:lo;hi=image.mips[0].data[i]>hi?image.mips[0].data[i]:hi;}
        std::printf("PASS: %s format=%u %ux%u mips=%u alpha range %u..%u\n",argv[1],image.format,image.width,image.height,image.count,lo,hi);}
    return 0;
}
