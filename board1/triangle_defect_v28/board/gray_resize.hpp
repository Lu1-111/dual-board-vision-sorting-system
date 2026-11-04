#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <stdexcept>
// Antialiased triangular two-pass 8-bit resize with Pillow-compatible rounding.
// Algorithm checked against Pillow's MIT-CMU licensed Resample.c.
namespace grayresize {
struct Coeff { int start; std::vector<int> weights; };
inline std::vector<Coeff> coefficients(int n, int m) {
    std::vector<Coeff> out(m); const double scale=(double)n/m, filter=std::max(scale,1.0);
    for(int x=0; x<m; ++x) {
        double center=(x+0.5)*scale; int lo=std::max(0,(int)(center-filter+0.5));
        int hi=std::min(n,(int)(center+filter+0.5)); std::vector<double> f; double sum=0;
        for(int j=lo;j<hi;++j){ double d=std::abs((j-center+0.5)/filter); double w=std::max(0.0,1.0-d);f.push_back(w);sum+=w; }
        out[x].start=lo; for(double w:f)out[x].weights.push_back((int)(0.5+(w/sum)*(1<<22)));
    } return out;
}
inline std::vector<uint8_t> resize(const uint8_t *src,int w,int h,int stride,int ow=224,int oh=224) {
    if(!src||w<1||h<1||stride<w||ow<1||oh<1)throw std::runtime_error("invalid image");
    auto hc=coefficients(w,ow), vc=coefficients(h,oh);std::vector<uint8_t> temp(ow*h),out(ow*oh);
    for(int y=0;y<h;y++)for(int x=0;x<ow;x++) { int acc=1<<21;auto &c=hc[x];for(size_t i=0;i<c.weights.size();i++)acc+=src[y*stride+c.start+i]*c.weights[i];temp[y*ow+x]=(uint8_t)std::max(0,std::min(255,acc>>22));}
    for(int y=0;y<oh;y++)for(int x=0;x<ow;x++) { int acc=1<<21;auto &c=vc[y];for(size_t i=0;i<c.weights.size();i++)acc+=temp[(c.start+i)*ow+x]*c.weights[i];out[y*ow+x]=(uint8_t)std::max(0,std::min(255,acc>>22));}
    return out;
}
}
