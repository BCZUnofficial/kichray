int main(){
    Pipeline pipeline;auto& objects=device.resources.resources;objects[2].pipeline=&pipeline;
    objects[3].info.kind=KICH_R4_VERTEX_BUFFER;objects[3].buffer_stride=64;
    objects[4].info.kind=KICH_R4_INDEX_BUFFER;objects[4].info.byte_size=12;objects[4].buffer_stride=4;
    objects[5].info.kind=KICH_R4_CONSTANT_BUFFER;objects[5].info.byte_size=DeferredConstantsBytes;
    objects[6].model_pose=true;objects[6].model_capacity=16;objects[6].info.byte_size=KichR4ModelPaletteBytes(16);
    ID3D11ShaderResourceView cookie,bump,aux;objects[7].srv=&cookie;objects[8].srv=&bump;objects[9].srv=&aux;
    KichR4ModelDrawDesc model={};model.struct_size=sizeof(model);model.pose_buffer=6;
    auto& desc=model.surface;desc.struct_size=sizeof(desc);desc.state.struct_size=sizeof(desc.state);auto& draw=desc.state.draw;
    draw.struct_size=sizeof(draw);draw.pipeline=2;draw.vertex_buffer=3;draw.index_buffer=4;draw.constant_buffer=5;draw.index_count=3;
    desc.state.source_blend=2;desc.state.dest_blend=1;desc.state.max_depth=1;desc.textures[3]=7;
    model.material_textures[0]=8;model.material_textures[1]=9;model.material_samplers[0]={1,1,1,2,2,2,1,0,0,0};model.material_samplers[1]=model.material_samplers[0];
    Require(DrawDeferredOrDetail(1,&desc,NULL,&model)==0 && pipeline.calls==1,"model extension accepted");
    Require(pipeline.observed[3]==&cookie && pipeline.observed[8]==&bump && pipeline.observed[9]==&aux && pipeline.samplers[0].u==1,"cookie and model normal maps delivered to distinct inputs");
    Require(device.bound_textures[8]==8 && device.bound_textures[9]==9,"provider telemetry tracks extra material resources");
    device.pass.depth=8;Require(DrawDeferredOrDetail(1,&desc,NULL,&model)==KICH_R4_E_ARGUMENT && pipeline.calls==1,"t8 depth read/write alias rejected before draw");device.pass.depth=0;
    device.pass.color_count=1;device.pass.colors[0]=9;Require(DrawDeferredOrDetail(1,&desc,NULL,&model)==KICH_R4_E_ARGUMENT && pipeline.calls==1,"t9 render target read/write alias rejected");device.pass.color_count=0;
    model.material_textures[0]=101;Require(DrawDeferredOrDetail(1,&desc,NULL,&model)==KICH_R4_E_HANDLE && pipeline.calls==1,"stale material handle rejected");model.material_textures[0]=8;
    objects[8].srv=NULL;Require(DrawDeferredOrDetail(1,&desc,NULL,&model)==KICH_R4_E_ARGUMENT,"resource without SRV rejected");objects[8].srv=&bump;
    model.material_samplers[0].u=99;Require(DrawDeferredOrDetail(1,&desc,NULL,&model)==KICH_R4_E_ARGUMENT && pipeline.calls==1,"invalid extra sampler rejected before draw");model.material_samplers[0].u=1;
    desc.textures[8]=8;Require(DrawDeferredOrDetail(1,&desc,NULL,&model)==KICH_R4_E_ARGUMENT && pipeline.calls==1,"generic and dedicated model slot collision rejected");desc.textures[8]=0;
    desc.extra_samplers[1]=model.material_samplers[1];Require(DrawDeferredOrDetail(1,&desc,NULL,&model)==KICH_R4_E_ARGUMENT,"generic and dedicated sampler collision rejected");desc.extra_samplers[1]={};
    pipeline.model=false;Require(DrawDeferredOrDetail(1,&desc,NULL)==0 && pipeline.calls==2,"ordinary draw remains compatible");
    Require(!pipeline.observed[8] && !pipeline.observed[9] && !device.bound_textures[8] && !device.bound_textures[9],"ordinary draw clears extra material resources");
    ID3D11ShaderResourceView layer3,receiver;objects[10].srv=&layer3;objects[11].srv=&receiver;
    for(unsigned i=0;i<KICH_R4_DEFERRED_EXTRA_SAMPLERS;++i){desc.textures[8+i]=8+i;desc.extra_samplers[i]={1,1,1,2,2,2,1,0,0,0};}
    Require(DrawDeferredOrDetail(1,&desc,NULL)==0 && pipeline.calls==3,"all four generic terrain inputs accepted");
    Require(pipeline.observed[8]==&bump && pipeline.observed[9]==&aux && pipeline.observed[10]==&layer3 && pipeline.observed[11]==&receiver && pipeline.samplers[3].u==1,"t8 through t11 and s9 through s12 delivered");
    device.pass.depth=11;Require(DrawDeferredOrDetail(1,&desc,NULL)==KICH_R4_E_ARGUMENT && pipeline.calls==3,"t11 depth alias rejected");device.pass.depth=0;
    device.pass.color_count=4;device.pass.colors[3]=11;Require(DrawDeferredOrDetail(1,&desc,NULL)==KICH_R4_E_ARGUMENT && pipeline.calls==3,"fourth MRT receiver cannot be simultaneously sampled");device.pass.color_count=0;
    desc.extra_samplers[3].u=99;Require(DrawDeferredOrDetail(1,&desc,NULL)==KICH_R4_E_ARGUMENT && pipeline.calls==3,"fourth generic sampler validated");desc.extra_samplers[3].u=1;
    desc.textures[11]=999;Require(DrawDeferredOrDetail(1,&desc,NULL)==KICH_R4_E_HANDLE && pipeline.calls==3,"fourth generic stale handle rejected");desc.textures[11]=11;
    model.material_textures[0]=model.material_textures[1]=0;model.material_samplers[0]=model.material_samplers[1]={};pipeline.model=true;
    Require(DrawDeferredOrDetail(1,&desc,NULL,&model)==0 && pipeline.calls==4 && pipeline.observed[11]==&receiver,"model without dedicated maps preserves explicit generic inputs");pipeline.model=false;
    for(unsigned i=0;i<KICH_R4_DEFERRED_EXTRA_SAMPLERS;++i){desc.textures[8+i]=0;desc.extra_samplers[i]={};}
    Require(DrawDeferredOrDetail(1,&desc,NULL)==0 && pipeline.calls==5,"ordinary draw after terrain accepted");
    for(unsigned i=8;i<DrawTextureSlots;++i)Require(!pipeline.observed[i] && !pipeline.samplers[i-8].u && !device.bound_textures[i],"ordinary draw clears all extra texture and sampler metadata");
    puts("PASS: actual provider model/cookie slot coexistence, extra RT/depth hazards, stale handles, sampler validation and ordinary unbind");
}
