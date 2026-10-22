#ifndef RENDER_SNAPSHOT_GEOMETRY_DATA_H
#define RENDER_SNAPSHOT_GEOMETRY_DATA_H
#include <stdint.h>
#include <math.h>
namespace snapshot_geometry {
inline bool Finite(float v){return v==v && v>=-3.402823466e38f && v<=3.402823466e38f;}
struct Checks {
    uint32_t normals=0,nonfinite=0,zero=0,bases=0,degenerate=0;
    void Normal(const float* n){++normals;if(!Finite(n[0]) || !Finite(n[1]) || !Finite(n[2])){++nonfinite;return;}const double len=double(n[0])*n[0]+double(n[1])*n[1]+double(n[2])*n[2];if(len<1e-12)++zero;}
    void Basis(const float* n,const float* t,const float* b){++bases;for(unsigned i=0;i<3;++i)if(!Finite(n[i]) || !Finite(t[i]) || !Finite(b[i])){++degenerate;return;}
        const double nl=double(n[0])*n[0]+double(n[1])*n[1]+double(n[2])*n[2],tl=double(t[0])*t[0]+double(t[1])*t[1]+double(t[2])*t[2],bl=double(b[0])*b[0]+double(b[1])*b[1]+double(b[2])*b[2];
        const double det=double(n[0])*(double(t[1])*b[2]-double(t[2])*b[1])-double(n[1])*(double(t[0])*b[2]-double(t[2])*b[0])+double(n[2])*(double(t[0])*b[1]-double(t[1])*b[0]);
        if(nl<1e-12 || tl<1e-12 || bl<1e-12 || det*det<1e-8*nl*tl*bl)++degenerate;}
};
inline double Determinant(const float* m){return double(m[0])*(double(m[5])*m[10]-double(m[6])*m[9])-double(m[1])*(double(m[4])*m[10]-double(m[6])*m[8])+double(m[2])*(double(m[4])*m[9]-double(m[5])*m[8]);}
inline bool Nonuniform(const float* m){double l[3]={};for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)l[i]+=double(m[i*4+j])*m[i*4+j];double maximum=l[0];if(l[1]>maximum)maximum=l[1];if(l[2]>maximum)maximum=l[2];const double tolerance=1e-5*(maximum>1?maximum:1);return fabs(l[0]-l[1])>tolerance || fabs(l[1]-l[2])>tolerance;}
}
#endif
