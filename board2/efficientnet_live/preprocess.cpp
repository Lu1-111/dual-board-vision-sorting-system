#include "preprocess.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#ifdef EFFICIENTNET_WITH_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif
#ifdef EFFICIENTNET_WITH_QT
#include <QImage>
#include <QImageReader>
#include <QString>
#endif

namespace efficientnet {
namespace {
constexpr int target=224;
constexpr int bits=22;
struct Tap { int begin; std::vector<int32_t> coefficients; };
// Bilinear downsampling is an antialiased separable triangle filter, matching
// Pillow 11's 8-bit path including intermediate rounding after each pass.
// Algorithm reference: Pillow src/libImaging/Resample.c (11.0.0).
std::vector<Tap> coefficients(int input,int output) {
    const double ratio=double(input)/output, radius=std::max(1.0,ratio);
    std::vector<Tap> result(output);
    for(int d=0;d<output;++d) {
        const double center=(d+0.5)*ratio;
        const int first=std::max(0,int(center-radius+0.5));
        const int last=std::min(input,int(center+radius+0.5));
        std::vector<double> weights(last-first); double sum=0;
        for(int s=first;s<last;++s) {const double w=std::max(0.0,1.0-std::abs((s-center+0.5)*(1.0/radius)));weights[s-first]=w;sum+=w;}
        Tap& t=result[d];t.begin=first;
        for(double w:weights)t.coefficients.push_back(int32_t(w/sum*double(1<<bits)+0.5));
    }
    return result;
}
int roundEven(double v) {
    const double lo=std::floor(v),rem=v-lo;
    return int(lo)+(rem>0.5 || (rem==0.5 && (int(lo)&1)));
}
uint8_t pixel(int64_t v) {return uint8_t(std::max<int64_t>(0,std::min<int64_t>(255,v>>bits)));}
}
std::vector<uint8_t> letterboxRgb(const uint8_t* rgb,int width,int height,std::size_t stride) {
    if(!rgb||width<1||height<1||width>16384||height>16384||stride<std::size_t(width)*3)throw std::runtime_error("Invalid full-frame RGB888 dimensions/stride");
    const double scale=std::min(double(target)/width,double(target)/height);
    const int w=std::max(1,roundEven(width*scale)),h=std::max(1,roundEven(height*scale));
    const auto horizontal=coefficients(width,w),vertical=coefficients(height,h);
    std::vector<uint8_t> temp(std::size_t(w)*height*3);
    for(int y=0;y<height;++y)for(int x=0;x<w;++x) {
        const Tap& t=horizontal[x];
        int64_t acc[3]={1<<(bits-1),1<<(bits-1),1<<(bits-1)};
        for(std::size_t k=0;k<t.coefficients.size();++k) {
            const uint8_t* p=rgb+std::size_t(y)*stride+std::size_t(t.begin+k)*3;
            for(int c=0;c<3;++c)acc[c]+=int64_t(p[c])*t.coefficients[k];
        }
        for(int c=0;c<3;++c)temp[(std::size_t(y)*w+x)*3+c]=pixel(acc[c]);
    }
    std::vector<uint8_t> result(target*target*3,114);
    const int left=(target-w)/2,top=(target-h)/2;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const Tap& t=vertical[y];
        int64_t acc[3]={1<<(bits-1),1<<(bits-1),1<<(bits-1)};
        for(std::size_t k=0;k<t.coefficients.size();++k) {
            const uint8_t* p=&temp[(std::size_t(t.begin+k)*w+x)*3];
            for(int c=0;c<3;++c)acc[c]+=int64_t(p[c])*t.coefficients[k];
        }
        for(int c=0;c<3;++c)result[(std::size_t(y+top)*target+x+left)*3+c]=pixel(acc[c]);
    }
    return result;
}
std::vector<float> normalizeRgb(const std::vector<uint8_t>& rgb224) {
    if(rgb224.size()!=target*target*3)throw std::runtime_error("Expected 224x224 RGB888");
    const float mean[3]={0.485f,0.456f,0.406f},sd[3]={0.229f,0.224f,0.225f};
    std::vector<float> out(3*target*target);
    for(int c=0;c<3;++c)for(int i=0;i<target*target;++i) {
        float f=float(rgb224[3*i+c])/255.0f;f=f-mean[c];out[c*target*target+i]=f/sd[c];
    }
    return out;
}
std::vector<float> prepareRgb(const uint8_t* rgb,int width,int height,std::size_t stride) {return normalizeRgb(letterboxRgb(rgb,width,height,stride));}
#ifdef EFFICIENTNET_WITH_OPENCV
std::vector<uint8_t> letterboxImageOpenCv(const std::string& path) {
    const cv::Mat bgr=cv::imread(path,cv::IMREAD_COLOR);
    if(bgr.empty())throw std::runtime_error("Cannot decode image: "+path);
    cv::Mat rgb;cv::cvtColor(bgr,rgb,cv::COLOR_BGR2RGB);
    return letterboxRgb(rgb.ptr<uint8_t>(),rgb.cols,rgb.rows,rgb.step);
}
std::vector<float> prepareImageOpenCv(const std::string& path) {return normalizeRgb(letterboxImageOpenCv(path));}
#endif
#ifdef EFFICIENTNET_WITH_QT
std::vector<float> prepareImageQt(const std::string& path) {
    QImageReader reader(QString::fromUtf8(path.c_str()));reader.setAutoTransform(true);
    QImage image=reader.read();
    if(image.isNull())throw std::runtime_error("Cannot decode image: "+path);
    image=image.convertToFormat(QImage::Format_RGB888);
    return prepareRgb(image.constBits(),image.width(),image.height(),std::size_t(image.bytesPerLine()));
}
#endif
}
