#include <JuceHeader.h>
#include "HarmonicEngine.h"

class Sequin2Processor final : public juce::AudioProcessor {
public:
    Sequin2Processor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),state(*this,nullptr,"State",makeParams()) {}
    void prepareToPlay(double sr,int maxBlock) override { ensureScratch(std::max(1,maxBlock)); engine.prepare(sr); dryL.fill(0);dryR.fill(0); dryPos=0; setLatencySamples(HarmonicEngine::N); }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& b) const override { auto c=b.getMainOutputChannelSet();return (c==juce::AudioChannelSet::mono()||c==juce::AudioChannelSet::stereo())&&c==b.getMainInputChannelSet(); }
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals nd;
        int n=buffer.getNumSamples();
        if(n>static_cast<int>(dryScratchL.size())) { buffer.clear(); return; }
        auto* l=buffer.getWritePointer(0);auto* r=buffer.getNumChannels()>1?buffer.getWritePointer(1):nullptr;
        float sens=state.getRawParameterValue("sensitivity")->load();
        float depth=state.getRawParameterValue("depth")->load();
        float lo=state.getRawParameterValue("low")->load();float hi=state.getRawParameterValue("high")->load();
        float mix=state.getRawParameterValue("mix")->load()/100.0f;
        bool bypass=state.getRawParameterValue("bypass")->load()>0.5f;
        engine.setControls(sens,depth,lo,hi);
        // Dry ring delayed exactly N samples to match the wet overlap-add output.
        for(int i=0;i<n;++i) {
            int p=(dryPos+i)%HarmonicEngine::N;
            float dl=dryL[p],dr=dryR[p];
            dryL[p]=l[i];dryR[p]=r?r[i]:l[i];
            dryScratchL[i]=dl;dryScratchR[i]=dr;
        }
        dryPos=(dryPos+n)%HarmonicEngine::N;
        engine.process(l,r,n);
        for(int i=0;i<n;++i) {
            l[i]=bypass?dryScratchL[i]:dryScratchL[i]*(1-mix)+l[i]*mix;
            if(r) r[i]=bypass?dryScratchR[i]:dryScratchR[i]*(1-mix)+r[i]*mix;
        }
        f0.store(engine.getF0());harmonics.store(engine.getActive());tooHot.store(engine.getHot());
    }
    using APVTS=juce::AudioProcessorValueTreeState;
    static APVTS::ParameterLayout makeParams() {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
        auto add=[&](const char* id,const char* label,float a,float b,float d){p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},label,juce::NormalisableRange<float>(a,b),d));};
        add("sensitivity","Excess threshold (dB)",0,18,5);
        add("depth","Maximum cut (dB)",0,12,6);
        add("low","Focus low (Hz)",20,10000,80);
        add("high","Focus high (Hz)",100,20000,16000);
        add("mix","Mix (%)",0,100,100);
        p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"Bypass",false));
        return {p.begin(),p.end()};
    }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override {return true;}
    const juce::String getName() const override{return "Sequin 2 Prototype";}
    bool acceptsMidi() const override{return false;}
    bool producesMidi() const override{return false;}
    bool isMidiEffect() const override{return false;}
    double getTailLengthSeconds() const override{return 0;}
    int getNumPrograms() override{return 1;}
    int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{}
    const juce::String getProgramName(int) override{return {};}
    void changeProgramName(int,const juce::String&) override{}
    void getStateInformation(juce::MemoryBlock& b) override {auto xml=state.copyState().createXml();copyXmlToBinary(*xml,b);}
    void setStateInformation(const void* data,int size) override {auto xml=getXmlFromBinary(data,size);if(xml&&xml->hasTagName(state.state.getType()))state.replaceState(juce::ValueTree::fromXml(*xml));}
    APVTS state;
    std::atomic<float> f0{0};std::atomic<int> harmonics{0},tooHot{0};
private:
    HarmonicEngine engine;
    std::array<float,HarmonicEngine::N> dryL{},dryR{};
    int dryPos=0;
    // Preallocated at prepareToPlay instead of allocating in audio callback.
    std::vector<float> dryScratchL,dryScratchR;
public:
    void ensureScratch(int maxBlock){dryScratchL.resize(maxBlock);dryScratchR.resize(maxBlock);}
};
class Sequin2Editor final : public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit Sequin2Editor(Sequin2Processor& p):AudioProcessorEditor(p),proc(p) {
        setSize(640,380);
        title.setText("SEQUIN 2  |  HARMONIC ENGINE PROTOTYPE",juce::dontSendNotification);
        title.setJustificationType(juce::Justification::centred);title.setColour(juce::Label::textColourId,juce::Colours::white);addAndMakeVisible(title);
        const char* ids[]={"sensitivity","depth","low","high","mix"};
        for(int i=0;i<5;++i) {
            auto* s=sliders.add(new juce::Slider());s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s->setTextBoxStyle(juce::Slider::TextBoxBelow,false,92,22);addAndMakeVisible(s);
            attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,ids[i],*s));
            auto* label=labels.add(new juce::Label());label->setText(ids[i],juce::dontSendNotification);label->setJustificationType(juce::Justification::centred);addAndMakeVisible(label);
        }
        bypass.setButtonText("Bypass");addAndMakeVisible(bypass);
        bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state,"bypass",bypass);
        stats.setJustificationType(juce::Justification::centred);addAndMakeVisible(stats);startTimerHz(12);
    }
    void paint(juce::Graphics& g) override {g.fillAll(juce::Colour(0xff101722));g.setColour(juce::Colour(0xff26374a));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(8),12,1);}
    void resized() override {title.setBounds(20,20,600,42);for(int i=0;i<5;++i){int x=16+i*122;labels[i]->setBounds(x,105,120,25);sliders[i]->setBounds(x,130,120,120);}stats.setBounds(70,270,500,35);bypass.setBounds(265,320,110,35);}
private:
    void timerCallback() override {stats.setText("Detected F0: "+juce::String(proc.f0.load(),1)+" Hz     Harmonics: "+juce::String(proc.harmonics.load())+"     Too hot: "+juce::String(proc.tooHot.load()),juce::dontSendNotification);}
    Sequin2Processor& proc;juce::Label title,stats;juce::OwnedArray<juce::Slider> sliders;juce::OwnedArray<juce::Label> labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    juce::ToggleButton bypass;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
};
juce::AudioProcessorEditor* Sequin2Processor::createEditor(){return new Sequin2Editor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Sequin2Processor();}
