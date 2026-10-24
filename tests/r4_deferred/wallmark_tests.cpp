#include "recording.h"
#include "../../xr_3da/KichR4WallmarkRules.h"
#include "../../xr_3da/KichR4Native.h"
uint32_t wallmark_scope=0;
// PRODUCTION_WALLMARK_SCOPE
namespace {
float saturate(float x){return std::max(0.f,std::min(1.f,x));}
float lerp(float a,float b,float x){return a+(b-a)*x;}
// PRODUCTION_WALLMARK_SHADER
void ColorContract(){using namespace kich_r4_wallmark;
    Require(Color("null",2,12)==FadeToVertex && Color("stub_default_ma",4,1)==FadeToVertex,"legacy stages and 1.6 wallmark shader share fade contract");
    Require(Color("simple",4,1)==TextureOnly && Color("null",2,1)==TextureOnly,"simple and SELECTARG1 ignore vertex gray/age");
    Require(Color("null",4,1)==Modulate && Color("null",5,1)==Modulate2x && Color("null",6,1)==Modulate4x,"SET BLEND MUL color variants remain distinct");
    const float gray=128.f/255.f;
    for(float receiver:{.05f,.4f,.95f})for(float light:{.1f,.8f,2.f}){
        const float neutral=WallmarkChannel(.5f,gray,0,FadeToVertex);
        Require(fabsf(2*neutral*receiver*light-receiver*light)<1.e-6f,"fresh neutral texel leaves lit receiver unchanged (no black square)");
        const float fresh=WallmarkChannel(.1f,gray,0,FadeToVertex),aged=WallmarkChannel(.1f,gray,.5f,FadeToVertex),expired=WallmarkChannel(.1f,gray,1,FadeToVertex);
        Require(fresh<aged && aged<expired && fabsf(expired-gray)<1.e-6f,"bullet fades toward neutral gray without multiplying it into fresh mark");
    }
    Require(WallmarkAlpha(.8f,0,FadeToVertex)==0 && fabsf(WallmarkAlpha(.8f,.5f,FadeToVertex)-.4f)<1.e-6f,"fade alpha matches original fixed-function source, independent of RGB multiply blend");
    Require(WallmarkChannel(.8f,.5f,.2f,TextureOnly)==.8f && WallmarkAlpha(.8f,.2f,TextureOnly)==.8f,"simple SET/BLEND keeps source color and alpha");
    Require(WallmarkChannel(.8f,.5f,.2f,Modulate)==.4f && fabsf(WallmarkAlpha(.8f,.2f,Modulate)-.16f)<1.e-6f,"ordinary modulate alpha is not mistaken for wallmark age");
    Require(WallmarkChannel(.8f,.5f,.2f,Modulate2x)==.8f && WallmarkChannel(.8f,.5f,.2f,Modulate4x)==1 && WallmarkAlpha(.8f,.2f,Modulate2x)==.8f,"2R/4R preserve clamp and texture alpha");
}
void Scope(){using namespace kich_r4_capture;Style style;Command c(0,3,0,"base",style);
    Require(!g_render_snapshot_active && wallmark_scope==0,"ordinary frame scope begins without telemetry");
    {KichR4NativeWallmarkScope outer(false);Require(wallmark_scope==1,"static source explicitly marks its draw");
        kich_r4_wallmark::Capture(wallmark_scope,false,4,c);Require(c.effect==6 && c.semantic==CAPTURE_STATIC_WALLMARK && c.wallmark_color==4,"null shader enters wallmark route without bWmark/provenance");
        try{KichR4NativeWallmarkScope inner(true);Require(wallmark_scope==2,"skeletal nested scope");
            kich_r4_wallmark::Capture(wallmark_scope,false,4,c);Require(c.semantic==CAPTURE_SKELETAL_WALLMARK,"skeletal classification independent of snapshot");throw 1;
        }catch(int){}Require(wallmark_scope==1,"exception restores outer scope");}
    Require(wallmark_scope==0,"end of batch resets scope before particles/UI");
    c.effect=1;c.semantic=CAPTURE_PARTICLE;kich_r4_wallmark::Capture(wallmark_scope,false,4,c);Require(c.effect==1 && c.semantic==CAPTURE_PARTICLE,"following particle unaffected");
    kich_r4_wallmark::Capture(1,true,4,c);Require(c.effect==1,"screen/UI draws not captured as decals");
}
void Frame(){using namespace kich_r4_capture;
    Fixture fixture;f=&fixture;ResetDeferredRecorder();const KichR4LevelFiles fs={Open,Close};kich_r4_game::Session game(DeferredAPIs(),fs,"l01_escape",NULL);game.deferred.native_mode=true;
    auto input=DeferredInput();input.lighting.count=2;for(unsigned i=0;i<2;++i){auto& point=input.lighting.lights[i];point.type=1;point.range=15;point.position[1]=2;point.color[i]=1;}
    Batch batch;batch.Begin(input.width,input.height,7);batch.renderer_seen=batch.ui_seen=true;
    const uint32_t sources[]={9,9,5,2},destinations[]={3,1,6,1},modes[]={4,0,3,1};
    for(unsigned j=0;j<4;++j){Style style;style.blend=1;style.src=sources[j];style.dst=destinations[j];style.write=0;style.alpha_func=j==2?5:8;style.alpha_ref=.01f;
        Command c(j*3,3,0,"base",style);c.shader="null";kich_r4_wallmark::Capture(j==1?2:1,false,modes[j],c);batch.commands.push_back(c);
        for(unsigned i=0;i<3;++i){Vertex v={};v.p[0]=i==0?-.1f:.1f;v.p[1]=i==1?.1f:-.1f;v.p[2]=.5f;v.p[3]=1;v.color=0x00808080;
            batch.indices.push_back(static_cast<uint32_t>(batch.vertices.size()));batch.vertices.push_back(v);}}
    Require(game.Render(input,&batch)==1,"native wallmark full frame succeeds without telemetry");
    unsigned count[4]={};size_t last_decal=0,first_light=draw_entries.size();
    for(size_t i=0;i<draw_entries.size();++i){const auto& d=draw_descriptions[i];
        if(draw_entries[i]=="PSSun" || draw_entries[i]=="PSLocal")first_light=std::min(first_light,i);
        if(d.state.draw.vertex_buffer!=game.stream.vb)continue;
        const unsigned j=d.state.draw.start_index/3;Require(j<4,"known decal stream range");++count[j];last_decal=i;
        Require(draw_entries[i]=="PSWallmark" && draw_constants[i].alpha[3]==modes[j],"explicit wallmark shader uses captured color contract");
        Require(d.state.source_blend==sources[j] && d.state.dest_blend==destinations[j],"MUL_2X MUL BLEND SET source blend preserved");
        Require(draw_constants[i].alpha[1]==batch.commands[j].style.alpha_func && draw_constants[i].alpha[0]==.01f,"original alpha comparison preserved");
        Require(d.state.surface.color_mask==7 && d.state.surface.stencil && !d.state.surface.stencil_write && !d.state.depth_write,"only receiver albedo RGB changes, alpha/normal/depth preserved");
    }
    for(unsigned j=0;j<4;++j)Require(count[j]==1,"each static/skeletal decal draws once, never per light/shadow");
    Require(last_decal<first_light,"decal albedo composed before sun/local lighting");
    KichR4GeometryReport report={};Require(game.Stop(report)==0 && f->objects.empty(),"wallmark fixture releases native resources");
}
}
int main(){try{(void)&Scenario;ColorContract();Scope();Frame();printf("E1.1 wallmark scope/color/receiver/order: %d checks passed (no GPU rasterization)\n",checks);return 0;}
catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
