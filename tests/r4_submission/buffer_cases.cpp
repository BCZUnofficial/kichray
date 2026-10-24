int main(){
    KichR4BufferDesc desc={sizeof(desc),KICH_R4_CONSTANT_BUFFER,912,0};KichR4Handle cb=0;
    Require(CreateBuffer(1,&desc,NULL,0,&cb)==0,"CB creation");Resource* r=device.resources.Find(cb);
    Require(r && r->buffer->desc.Usage==D3D11_USAGE_DYNAMIC && r->constant_contents.size()==912,"dynamic CB creation contract");
    unsigned char bytes[912]={};Require(UpdateBuffer(1,cb,bytes,sizeof(bytes))==0 && device.ctx.maps==1,"initial zero buffer must upload");
    const uint32_t generation=r->generation;Require(UpdateBuffer(1,cb,bytes,sizeof(bytes))==0,"same CB update");
    Require(device.ctx.maps==1 && r->generation==generation && device.unchanged_constant_updates==1,"identical CB write not skipped");
    float alpha[4]={.25f,7,0,1};memcpy(bytes+800,alpha,sizeof(alpha));Require(UpdateBuffer(1,cb,bytes,sizeof(bytes))==0 && device.ctx.maps==2,"changed CB upload");
    Require(!memcmp(r->alpha,alpha,sizeof(alpha)) && !memcmp(r->buffer->bytes.data(),bytes,sizeof(bytes)),"CB alpha/payload out of sync");
    const auto previous=r->constant_contents;const uint32_t last_generation=r->generation;bytes[0]=9;device.ctx.failure=-123;
    Require(UpdateBuffer(1,cb,bytes,sizeof(bytes))==-123,"Map failure swallowed");
    Require(r->constant_contents==previous && r->generation==last_generation,"failed Map changed dirty shadow");
    device.ctx.failure=0;Require(UpdateBuffer(1,cb,bytes,sizeof(bytes))==0 && device.ctx.maps==3 && r->constant_contents[0]==9,"retry after Map failure skipped");
    KichR4Handle initialized=0;Require(CreateBuffer(1,&desc,bytes,sizeof(bytes),&initialized)==0,"initial-data CB");
    const unsigned maps=device.ctx.maps;Require(UpdateBuffer(1,initialized,bytes,sizeof(bytes))==0 && device.ctx.maps==maps,"initial data not included in dirty shadow");
    desc.kind=KICH_R4_VERTEX_BUFFER;desc.byte_size=16;desc.stride=4;KichR4Handle vb=0;Require(CreateBuffer(1,&desc,bytes,16,&vb)==0,"VB creation");
    Require(UpdateBuffer(1,vb,bytes,16)==0 && device.ctx.updates==1 && device.resources.Find(vb)->buffer->desc.Usage==D3D11_USAGE_DEFAULT,"VB update path changed");
    Require(UpdateBuffer(1,cb,bytes,16)==KICH_R4_E_ARGUMENT,"CB size validation removed");
    r->model_pose=true;Require(UpdateBuffer(1,cb,bytes,sizeof(bytes))==KICH_R4_E_ARGUMENT,"model palette bypassed ownership");r->model_pose=false;
    r->dynamic_instances=true;Require(UpdateBuffer(1,cb,bytes,sizeof(bytes))==KICH_R4_E_ARGUMENT,"instance buffer bypassed ownership");
    Require(device.ctx.maps==device.ctx.unmaps && device.uploads==device.geometry_stats.buffer_updates,"actual upload accounting mismatch");
    puts("PASS: actual CreateBuffer/UpdateBuffer, DX11.0 discard, identical suppression, initial data, failed Map retry, alpha metadata, DEFAULT VB and ownership validation");
}
