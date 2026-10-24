// Independent float-vertex raster oracle. Unlike the old analytic-plane depth
// test, this includes per-triangle clip rounding and D24 receiver quantization.
// It does not emulate driver raster subpixel precision or fused instructions.
struct ReceiverRasterPlane {
    float4 clip[4];
    ReceiverRasterPlane(float slope,float ground,const kich_r4_scene::Matrix& vp){
        const float x[4]={-500,500,500,-500},z[4]={20,20,-700,-700};
        for(unsigned i=0;i<4;++i)clip[i]=Transform(float3(x[i],ground+slope*(x[i]-122.005f),z[i]),1,vp);
    }
    bool Sample(float2 pixel,float& depth,float& linear_depth)const{
        const double px=double(pixel.x)*2/screen.x-1,py=1-double(pixel.y)*2/screen.y;
        const unsigned indices[2][3]={{0,1,2},{0,2,3}};
        for(unsigned t=0;t<2;++t){double x[3],y[3],z[3];
            for(unsigned i=0;i<3;++i){const float4 p=clip[indices[t][i]];x[i]=double(p.x)/p.w;y[i]=double(p.y)/p.w;z[i]=double(p.z)/p.w;}
            const double determinant=(y[1]-y[2])*(x[0]-x[2])+(x[2]-x[1])*(y[0]-y[2]);
            const double a=((y[1]-y[2])*(px-x[2])+(x[2]-x[1])*(py-y[2]))/determinant;
            const double b=((y[2]-y[0])*(px-x[2])+(x[0]-x[2])*(py-y[2]))/determinant;
            if(a<0 || b<0 || a+b>1)continue;
            depth=float(std::round((a*z[0]+b*z[1]+(1-a-b)*z[2])*16777215)/16777215);
            // D3D11 functional spec 16.3: PS position.w is reconstructed clip W.
            linear_depth=float(1/(a/clip[indices[t][0]].w+b/clip[indices[t][1]].w+(1-a-b)/clip[indices[t][2]].w));
            return true;
        }
        return false;
    }
};
void ReceiverRasterization(){using namespace kich_r4_lighting;
    const float direction[3]={-.804887f,-.420196f,-.419049f};
    const float3 sun(direction[0],direction[1],direction[2]);const float ground=-3.313f;
    screen=float4(1920,1080,0,2048);unsigned checked=0;
    for(float slope:{0.f,.1f,.3f,.4f,.5f})for(float near_plane:{.2f,.01f})for(unsigned frame=0;frame<40;++frame){
        const float origin[3]={122.005f+.01f*frame,-1.783f,35.856f};camera=float4(origin[0],origin[1],origin[2],0);
        const Vec3 forward=Normalize(Vec3{.0107f+.0001f*frame,-.0645f,-.9979f});
        Matrix projection=Projection(67.5f*3.14159265358979323846f/180,near_plane,635.5106f);projection.m[0]*=1080.f/1920;
        const Matrix vp=Multiply(StableView(Vec3{origin[0],origin[1],origin[2]},forward),projection);
        Require(Inverse(vp,inverse_vp,origin) && ReceiverInverse(vp,receiver_inverse,origin),"both raster-compatible receiver inverses exist");
        const ReceiverRasterPlane raster(slope,ground,vp);
        const Matrix sm=SunShadow(origin,direction,512,2048);Matrix sm_inverse;Require(Inverse(sm,sm_inverse),"shadow oracle inverse exists");
        const double A=sm_inverse.m[1]-slope*sm_inverse.m[0],B=sm_inverse.m[5]-slope*sm_inverse.m[4];
        const double D=sm_inverse.m[9]-slope*sm_inverse.m[8],T=sm_inverse.m[13]-slope*sm_inverse.m[12]-(ground-slope*122.005);
        Texture2D plane={-2*A/D,2*B/D,(A-B-T)/D,false},blocker=plane;
        blocker.c+=.02*std::sqrt(1+double(slope)*slope)/D;
        for(unsigned y=450;y<900;y+=4)for(unsigned x=400;x<1500;x+=20){
            float3 relative[4];bool valid=true;
            for(unsigned k=0;k<4;++k){const float2 pixel(float(x)+.5f+float(k&1),float(y)+.5f+float(k>>1));float depth=0,linear_depth=0;
                if(!raster.Sample(pixel,depth,linear_depth)){valid=false;break;}
                relative[k]=ReceiverPosition(pixel,linear_depth,depth);
            }
            if(!valid)continue;
            const float3 p=relative[0]+xyz(camera),dx=relative[1]-relative[0],dy=relative[2]-relative[0];
            const float3 mixed=relative[3]-relative[1]-relative[2]+relative[0];
            if(dot(relative[0],relative[0])>300*300 || dot(relative[0],float3(forward.x,forward.y,forward.z))<=0)continue;
            const float3 offset=ShadowNumericalOffset(p,dx,dy,-sun);
            Require(length(offset-p)<=.0021f,"precision allowance is bounded to two millimetres in world space");
            const float4 q=Transform(offset-sun*.01f,1,sm);
            if(std::max(std::fabs(q.x),std::fabs(q.y))>=.9f)continue;
            Require(std::fabs(p.y-(ground+slope*(p.x-122.005f)))<.0002,"linear receiver avoids triangle-sized D24 reconstruction errors");
            Require(ShadowPlaneStable(dot(relative[0],relative[0]),max(dot(dx,dx),dot(dy,dy)),ShadowPlaneErrorSquared(dx,dy,mixed)),"moving continuous ground retains its plane");
            Require(PCF(plane,q,Transform(dx,0,sm),Transform(dy,0,sm))>.9999f,"moving rasterized unoccluded plane stays lit at near0.01 and grazing incidence");
            Require(PCF(blocker,q,Transform(dx,0,sm),Transform(dy,0,sm))<.0001f,"two-centimetre contact blocker survives the precision repair");
            ++checked;
        }
    }
    Require(checked>100000,"temporal receiver regression covers terrain and both near-plane settings");
    const float origin[3]={0,0,0};Require(!ReceiverInverse(Identity(),receiver_inverse,origin),"constant-W orthographic path reports unsupported affine reconstruction");
    Require(receiver_inverse.m[15]==0,"unsupported projection clears its validity flag");
    inverse_vp=Identity();const float2 pixel(960,540);
    Require(length(ReceiverPosition(pixel,100,.5f)-RelativePosition(pixel,.5f))==0,"orthographic compatibility falls back to exact D24 inverse");
    std::printf("E1.9 moving triangle receiver/contact: %u samples passed\n",checked);
}
