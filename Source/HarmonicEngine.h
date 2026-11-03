#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <algorithm>

// Prototype: mono-linked stereo STFT; 4096 samples, 4x overlap.
// Harmonic salience is evaluated at integer multiples of an autocorrelation-estimated F0.
class HarmonicEngine {
public:
    static constexpr int N=4096, H=N/4, B=N/2+1;
    void prepare(double sr) {
        sampleRate=sr;
        fft=std::make_unique<juce::dsp::FFT>(12);
        for(auto& c: channels) { c.input.fill(0); c.output.fill(0); c.work.fill(0); }
        position=0; f0=0; active=0; hot=0; gains.fill(1.0f);
        window.fill(0);
        for(int i=0;i<N;++i) window[i]=0.5f-0.5f*std::cos(2.0*juce::MathConstants<double>::pi*i/N);
    }
    void setControls(float sensitivity_,float depth_,float low_,float high_) {
        sensitivity=sensitivity_; depth=depth_; low=low_; high=high_;
    }
    void process(float* left,float* right,int count) {
        for(int s=0;s<count;++s) {
            const int slot=position%N;
            float a=left[s],b=right?right[s]:a;
            left[s]=channels[0].output[slot]; channels[0].output[slot]=0;
            if(right) { right[s]=channels[1].output[slot]; channels[1].output[slot]=0; }
            channels[0].input[slot]=a;
            channels[1].input[slot]=b;
            ++position;
            if(position%H==0) frame(right!=nullptr);
        }
    }
    float getF0() const { return f0; }
    int getActive() const { return active; }
    int getHot() const { return hot; }
private:
    struct Channel { std::array<float,N> input{},output{}; std::array<float,2*N> work{}; };
    std::array<Channel,2> channels;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::array<float,N> window{};
    std::array<float,B> gains{}, mags{}, harmonicDb{};
    double sampleRate=48000;
    int64_t position=0;
    float sensitivity=5,depth=6,low=80,high=16000,f0=0;
    int active=0,hot=0;
    void frame(bool stereo) {
        // Frame ends at current position; reconstruction is delayed by one full window.
        for(int ch=0;ch<(stereo?2:1);++ch) {
            auto& c=channels[ch]; c.work.fill(0);
            for(int i=0;i<N;++i) c.work[i]=c.input[(position+i)%N]*window[i];
            fft->performRealOnlyForwardTransform(c.work.data());
        }
        auto& a=channels[0].work;
        for(int k=0;k<B;++k) {
            float re=a[2*k], im=a[2*k+1];
            mags[k]=std::sqrt(re*re+im*im)+1.0e-12f;
        }
        estimateF0();
        gains.fill(1);
        active=0; hot=0;
        if(f0>0) {
            const float binHz=static_cast<float>(sampleRate/N);
            const int maxH=std::min(100,static_cast<int>((sampleRate*0.5)/f0));
            std::array<float,101> levels{};
            std::array<int,101> bins{};
            for(int h=1;h<=maxH;++h) {
                int k=juce::jlimit(2,B-3,static_cast<int>(std::round(h*f0/binHz)));
                int best=k;
                for(int j=k-1;j<=k+1;++j) if(mags[j]>mags[best]) best=j;
                bins[h]=best;
                levels[h]=20.0f*std::log10(mags[best]);
            }
            for(int h=2;h<maxH;++h) {
                const float hz=h*f0;
                if(hz<low||hz>high) continue;
                ++active;
                // Neighbour-harmonic reference: exclude the candidate from its own baseline.
                const float reference=0.5f*(levels[h-1]+levels[h+1]);
                const float excess=levels[h]-reference-sensitivity;
                if(excess<=0) continue;
                ++hot;
                const float reduction=std::min(depth,excess*0.8f);
                const int center=bins[h];
                // Narrow smooth taper, rather than a one-bin hard discontinuity.
                const float radius=std::max(1.5f,0.18f*f0/binHz);
                const int r=static_cast<int>(std::ceil(2.5f*radius));
                for(int k=std::max(1,center-r);k<std::min(B-1,center+r+1);++k) {
                    const float x=(k-center)/radius;
                    const float db=reduction*std::exp(-0.5f*x*x);
                    gains[k]=std::min(gains[k],std::pow(10.0f,-db/20.0f));
                }
            }
        }
        for(int ch=0;ch<(stereo?2:1);++ch) {
            auto& c=channels[ch];
            for(int k=1;k<B-1;++k) { c.work[2*k]*=gains[k]; c.work[2*k+1]*=gains[k]; }
            fft->performRealOnlyInverseTransform(c.work.data());
            // Hann^2 overlap sum = 1.5 for 4x overlap; normalize by 2/3.
            // Output is scheduled one full FFT window behind input.
            for(int i=0;i<N;++i) {
                int out=static_cast<int>((position+i)%N);
                c.output[out]+=c.work[i]*window[i]*(2.0f/3.0f);
            }
        }
    }
    void estimateF0() {
        // Time-domain normalized autocorrelation of latest frame. Restricted to vocal F0 range.
        const int minLag=std::max(1,static_cast<int>(sampleRate/900.0));
        const int maxLag=std::min(N/2,static_cast<int>(sampleRate/75.0));
        float best=0; int bestLag=0;
        const auto& x=channels[0].input;
        for(int lag=minLag;lag<=maxLag;++lag) {
            double xy=0,xx=0,yy=0;
            for(int i=0;i<N-lag;i+=2) {
                float u=x[(position+i)%N],v=x[(position+i+lag)%N];
                xy+=u*v; xx+=u*u; yy+=v*v;
            }
            float corr=static_cast<float>(xy/std::sqrt(xx*yy+1.e-20));
            if(corr>best) { best=corr;bestLag=lag; }
        }
        // Hold no pitch on unvoiced frames; avoids falsely identifying arbitrary spectral peaks.
        f0=(best>0.72f&&bestLag>0)?static_cast<float>(sampleRate/bestLag):0.0f;
    }
};
