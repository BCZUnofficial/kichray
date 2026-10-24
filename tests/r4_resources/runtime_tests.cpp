#include "runtime_under_test.cpp"
int main(){try{unsigned char data[24]={1,2,3,4,5,6,7,8,0,0,0,0,9,10,11,12,13,14,15,16};uint64_t h=0,next=0;
    Require(UploadBGRA8(1,0,2,2,data,12,20,&h)==0 && h,"create with borrowed padded rows");
    auto* t=device.resources.Find(h)->texture;Require(t->data[0]==3 && t->data[2]==1 && t->data[24]==11 && t->data[27]==12,"BGRA conversion respects source and mapped row pitches");
    for(unsigned i=0;i<100;++i)Require(UploadBGRA8(1,h,2,2,data,12,20,&next)==0 && next==h,"revision update keeps handle");
    Require(device.hardware.creates==1 && device.ctx.maps==101 && device.ctx.unmaps==101,"one allocation and balanced discard updates");
    Require(UploadBGRA8(1,h,2,2,data,7,20,&next)<0 && !next && device.hardware.creates==1,"bad pitch rejected before touching last-good");
    const auto previous=t->data;device.ctx.failure=KICH_R4_E_MEMORY;
    Require(UploadBGRA8(1,h,2,2,data,12,20,&next)==KICH_R4_E_MEMORY && device.resources.Find(h)->texture->data==previous,"failed map preserves last-good texture");device.ctx.failure=0;
    Require(UploadBGRA8(1,h,1,1,data,4,4,&next)==0 && next!=h && device.resources.Find(h),"resize publishes replacement without releasing caller-owned old handle");
    Require(UploadBGRA8(1,999,1,1,data,4,4,&next)==KICH_R4_E_HANDLE && !next,"stale handle rejected");
    Resource shader;KichR4ShaderBinding binding={};binding.kind=KICH_R4_BIND_SAMPLER;binding.count=1;uint32_t bytes=0;
    for(unsigned i=0;i<9;++i){binding.slot=i;shader.bindings.push_back(binding);}Require(ShaderConstants(shader,bytes,true,true)==0,"real shader reflection gate accepts surface samplers and reserved comparison sampler");
    binding.slot=9;shader.bindings.push_back(binding);Require(ShaderConstants(shader,bytes,true,true)<0,"undeclared sampler slots remain rejected");
    puts("PASS: production runtime upload body: reuse, resize, padded BGRA rows, failure retention, invalid handle");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
