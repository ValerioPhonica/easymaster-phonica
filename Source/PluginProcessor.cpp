// ═══════════════════════════════════════════════════════════════
//  EASY MASTER — Phonica School
//  All implementations consolidated
// ═══════════════════════════════════════════════════════════════

#include "PluginProcessor.h"

// ─────────────────────────────────────────────────────────────
//  INPUT STAGE
// ─────────────────────────────────────────────────────────────

void InputStage::prepare (double sr, int bs)
{
    currentSampleRate = sr; currentBlockSize = bs;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32)bs, 2 };
    crossoverLP.prepare (spec); crossoverHP.prepare (spec);
    crossoverLP.setCutoffFrequency (crossoverFreq.load());
    crossoverHP.setCutoffFrequency (crossoverFreq.load());
    crossoverLP.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    crossoverHP.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lowBand.setSize (2, bs); highBand.setSize (2, bs);
}

void InputStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (! stageOn.load()) return;
    updateInputMeters (block);
    const int n = (int)block.getNumSamples();

    double gain = inputGain.load (std::memory_order_relaxed);
    if (std::abs(gain - 1.0) > 1e-6) block.multiplyBy (gain);

    lowBand.setSize (2, n, false, false, true);
    highBand.setSize (2, n, false, false, true);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* src = block.getChannelPointer (ch);
        juce::FloatVectorOperations::copy (lowBand.getWritePointer(ch), src, n);
        juce::FloatVectorOperations::copy (highBand.getWritePointer(ch), src, n);
    }

    { juce::dsp::AudioBlock<double> b(lowBand); juce::dsp::ProcessContextReplacing<double> c(b); crossoverLP.process(c); }
    { juce::dsp::AudioBlock<double> b(highBand); juce::dsp::ProcessContextReplacing<double> c(b); crossoverHP.process(c); }

    double lw = lowWidth.load() / 100.0, hw = highWidth.load() / 100.0;
    double mg = midGain.load(), sg = sideGain.load();

    auto applyWidth = [&](juce::AudioBuffer<double>& band, double width)
    {
        auto* l = band.getWritePointer(0); auto* r = band.getWritePointer(1);
        for (int i = 0; i < n; ++i)
        {
            double mid = (l[i]+r[i])*0.5, side = (l[i]-r[i])*0.5;
            mid *= mg; side *= sg * width;
            l[i] = mid+side; r[i] = mid-side;
        }
    };
    applyWidth (lowBand, lw); applyWidth (highBand, hw);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* dst = block.getChannelPointer(ch);
        for (int i = 0; i < n; ++i)
            dst[i] = lowBand.getSample(ch,i) + highBand.getSample(ch,i);
    }

    // Correlation
    auto* cL = block.getChannelPointer(0); auto* cR = block.getChannelPointer(1);
    double sLR=0, sLL=0, sRR=0;
    for (int i=0;i<n;++i) { sLR+=cL[i]*cR[i]; sLL+=cL[i]*cL[i]; sRR+=cR[i]*cR[i]; }
    double d=std::sqrt(sLL*sRR);
    correlation.store(d>1e-12 ? (float)(sLR/d) : 1.0f);

    updateOutputMeters (block);
}

void InputStage::reset() { crossoverLP.reset(); crossoverHP.reset(); }

int InputStage::getLatencySamples() const { return crossoverMode.load()==1 ? 512 : 0; }

void InputStage::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S1_Input_On","Input On",true));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S1_Input_Gain","Input Gain",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S1_Input_Crossover","Crossover",juce::NormalisableRange<float>(20,2000,1,0.3f),300));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S1_Input_Low_Width","Low Width",juce::NormalisableRange<float>(0,200,1),100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S1_Input_High_Width","High Width",juce::NormalisableRange<float>(0,200,1),100));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S1_Input_Crossover_Mode","Crossover Mode",juce::StringArray{"Min Phase","Linear Phase"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S1_Input_Mid_Gain","Mid Gain",juce::NormalisableRange<float>(-6,6,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S1_Input_Side_Gain","Side Gain",juce::NormalisableRange<float>(-6,6,0.1f),0));
}

void InputStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S1_Input_On")->load()>0.5f);
    inputGain.store(juce::Decibels::decibelsToGain((double)a.getRawParameterValue("S1_Input_Gain")->load()));
    float xf=a.getRawParameterValue("S1_Input_Crossover")->load();
    crossoverFreq.store(xf); crossoverLP.setCutoffFrequency(xf); crossoverHP.setCutoffFrequency(xf);
    lowWidth.store(a.getRawParameterValue("S1_Input_Low_Width")->load());
    highWidth.store(a.getRawParameterValue("S1_Input_High_Width")->load());
    crossoverMode.store((int)a.getRawParameterValue("S1_Input_Crossover_Mode")->load());
    midGain.store(juce::Decibels::decibelsToGain((double)a.getRawParameterValue("S1_Input_Mid_Gain")->load()));
    sideGain.store(juce::Decibels::decibelsToGain((double)a.getRawParameterValue("S1_Input_Side_Gain")->load()));
}

// ─────────────────────────────────────────────────────────────
//  PULTEC EQ STAGE
// ─────────────────────────────────────────────────────────────

void PultecEQStage::prepare (double sr, int bs)
{
    currentSampleRate = sr; currentBlockSize = bs;
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)bs,1};
    lowBoostL.prepare(spec); lowBoostR.prepare(spec); lowAttenL.prepare(spec); lowAttenR.prepare(spec);
    highBoostL.prepare(spec); highBoostR.prepare(spec); highAttenL.prepare(spec); highAttenR.prepare(spec);
    lowMidL.prepare(spec); lowMidR.prepare(spec); midDipL.prepare(spec); midDipR.prepare(spec);
    highMidL.prepare(spec); highMidR.prepare(spec);
    updateFilters();
}

void PultecEQStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters(block);
    int n=(int)block.getNumSamples();
    auto* l=block.getChannelPointer(0); auto* r=block.getChannelPointer(1);
    for (int i=0;i<n;++i)
    {
        double L=l[i], R=r[i];
        L=lowBoostL.processSample(L); R=lowBoostR.processSample(R);
        L=lowAttenL.processSample(L); R=lowAttenR.processSample(R);
        L=highBoostL.processSample(L); R=highBoostR.processSample(R);
        L=highAttenL.processSample(L); R=highAttenR.processSample(R);
        L=lowMidL.processSample(L); R=lowMidR.processSample(R);
        L=midDipL.processSample(L); R=midDipR.processSample(R);
        L=highMidL.processSample(L); R=highMidR.processSample(R);
        l[i]=L; r[i]=R;
    }
    updateOutputMeters(block);
}

void PultecEQStage::reset()
{
    lowBoostL.reset();lowBoostR.reset();lowAttenL.reset();lowAttenR.reset();
    highBoostL.reset();highBoostR.reset();highAttenL.reset();highAttenR.reset();
    lowMidL.reset();lowMidR.reset();midDipL.reset();midDipR.reset();
    highMidL.reset();highMidR.reset();
}

void PultecEQStage::updateFilters()
{
    double sr=currentSampleRate; if(sr<=0)return;
    auto lb=juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr,lowBoostFreq.load(),0.5,juce::Decibels::decibelsToGain((double)lowBoostGain.load()));
    *lowBoostL.coefficients=*lb; *lowBoostR.coefficients=*lb;
    auto la=juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr,lowAttenFreq.load(),1.0,juce::Decibels::decibelsToGain(-(double)lowAttenGain.load()));
    *lowAttenL.coefficients=*la; *lowAttenR.coefficients=*la;
    auto hb=juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr,highBoostFreq.load(),0.6,juce::Decibels::decibelsToGain((double)highBoostGain.load()));
    *highBoostL.coefficients=*hb; *highBoostR.coefficients=*hb;
    auto ha=juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr,highAttenFreq.load(),(double)highAttenBW.load(),juce::Decibels::decibelsToGain(-(double)highBoostGain.load()));
    *highAttenL.coefficients=*ha; *highAttenR.coefficients=*ha;
    auto lm=juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr,lowMidFreq.load(),1.0,juce::Decibels::decibelsToGain((double)lowMidGain.load()));
    *lowMidL.coefficients=*lm; *lowMidR.coefficients=*lm;
    auto md=juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr,midDipFreq.load(),1.5,juce::Decibels::decibelsToGain((double)midDipGain.load()));
    *midDipL.coefficients=*md; *midDipR.coefficients=*md;
    auto hm=juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr,highMidFreq.load(),1.0,juce::Decibels::decibelsToGain((double)highMidGain.load()));
    *highMidL.coefficients=*hm; *highMidR.coefficients=*hm;
}

void PultecEQStage::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S2_EQ_On","Pultec EQ On",true));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowBoost_Freq","LB Freq",juce::NormalisableRange<float>(20,200,1,0.5f),60));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowBoost_Gain","LB Gain",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowAtten_Freq","LA Freq",juce::NormalisableRange<float>(20,200,1,0.5f),60));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowAtten_Gain","LA Gain",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighBoost_Freq","HB Freq",juce::NormalisableRange<float>(1000,16000,1,0.3f),8000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighBoost_Gain","HB Gain",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighAtten_Freq","HA Freq",juce::NormalisableRange<float>(1000,16000,1,0.3f),8000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighAtten_BW","HA BW",juce::NormalisableRange<float>(0.1f,4,0.01f),1));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowMid_Freq","LM Freq",juce::NormalisableRange<float>(100,1000,1,0.4f),200));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowMid_Gain","LM Gain",juce::NormalisableRange<float>(-10,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_MidDip_Freq","MD Freq",juce::NormalisableRange<float>(200,5000,1,0.35f),1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_MidDip_Gain","MD Gain",juce::NormalisableRange<float>(-10,0,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighMid_Freq","HM Freq",juce::NormalisableRange<float>(1000,8000,1,0.3f),3000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighMid_Gain","HM Gain",juce::NormalisableRange<float>(-10,10,0.1f),0));
}

void PultecEQStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S2_EQ_On")->load()>0.5f);
    lowBoostFreq.store(a.getRawParameterValue("S2_EQ_LowBoost_Freq")->load());
    lowBoostGain.store(a.getRawParameterValue("S2_EQ_LowBoost_Gain")->load());
    lowAttenFreq.store(a.getRawParameterValue("S2_EQ_LowAtten_Freq")->load());
    lowAttenGain.store(a.getRawParameterValue("S2_EQ_LowAtten_Gain")->load());
    highBoostFreq.store(a.getRawParameterValue("S2_EQ_HighBoost_Freq")->load());
    highBoostGain.store(a.getRawParameterValue("S2_EQ_HighBoost_Gain")->load());
    highAttenFreq.store(a.getRawParameterValue("S2_EQ_HighAtten_Freq")->load());
    highAttenBW.store(a.getRawParameterValue("S2_EQ_HighAtten_BW")->load());
    lowMidFreq.store(a.getRawParameterValue("S2_EQ_LowMid_Freq")->load());
    lowMidGain.store(a.getRawParameterValue("S2_EQ_LowMid_Gain")->load());
    midDipFreq.store(a.getRawParameterValue("S2_EQ_MidDip_Freq")->load());
    midDipGain.store(a.getRawParameterValue("S2_EQ_MidDip_Gain")->load());
    highMidFreq.store(a.getRawParameterValue("S2_EQ_HighMid_Freq")->load());
    highMidGain.store(a.getRawParameterValue("S2_EQ_HighMid_Gain")->load());
    updateFilters();
}

// ─────────────────────────────────────────────────────────────
//  COMPRESSOR STAGE
// ─────────────────────────────────────────────────────────────

void CompressorStage::prepare (double sr, int bs)
{
    currentSampleRate=sr; currentBlockSize=bs; envelope=0;
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)bs,1};
    scHpL.prepare(spec); scHpR.prepare(spec);
    updateCoefficients();
}

void CompressorStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters(block);
    int n=(int)block.getNumSamples();
    auto* l=block.getChannelPointer(0); auto* r=block.getChannelPointer(1);
    double dryMix=1.0-(mix.load()/100.0), wetMix=mix.load()/100.0;
    double makeup=juce::Decibels::decibelsToGain((double)makeupGain.load());

    for (int i=0;i<n;++i)
    {
        double dL=l[i], dR=r[i];
        double sL=scHpL.processSample(l[i]), sR=scHpR.processSample(r[i]);
        double scDb=juce::Decibels::gainToDecibels(std::max(std::abs(sL),std::abs(sR)),-100.0);

        if(scDb>envelope) envelope=attackCoeff*envelope+(1-attackCoeff)*scDb;
        else
        {
            double relCoeff = releaseCoeff;
            if (autoRelease.load())
            {
                double grAmt = std::abs(computeGainReduction(envelope));
                double ms = juce::jlimit(30.0, 500.0, juce::jmap(grAmt, 0.0, 12.0, 50.0, 300.0));
                relCoeff = std::exp(-1.0 / (currentSampleRate * ms / 1000.0));
            }
            envelope = relCoeff*envelope+(1-relCoeff)*scDb;
        }

        double gr=computeGainReduction(envelope);
        double grLin=juce::Decibels::decibelsToGain(gr);
        l[i]=dL*dryMix+l[i]*grLin*makeup*wetMix;
        r[i]=dR*dryMix+r[i]*grLin*makeup*wetMix;
        meterData.gainReduction.store((float)gr);
    }
    updateOutputMeters(block);
}

void CompressorStage::reset() { envelope=0; scHpL.reset(); scHpR.reset(); }

double CompressorStage::computeGainReduction (double inputLevel)
{
    double t=threshold.load(), r=ratio.load();
    if (inputLevel<=t) return 0;
    double over=inputLevel-t;
    switch(model.load())
    {
        case 0: return -(over*(1.0-1.0/r));
        case 1: { double k=6; return over<k ? -(over*over/(2*k))*(1-1.0/r) : -(over*(1-1.0/r)); }
        case 2: { double er=r*(1+over/40); return -(over*(1-1.0/er)); }
        case 3: { double so=over*over/(over+4); return -(so*(1-1.0/r)); }
        default: return -(over*(1-1.0/r));
    }
}

void CompressorStage::updateCoefficients()
{
    double sr=currentSampleRate; if(sr<=0)return;
    attackCoeff=std::exp(-1.0/(sr*attackMs.load()/1000.0));
    releaseCoeff=std::exp(-1.0/(sr*releaseMs.load()/1000.0));
    auto c=juce::dsp::IIR::Coefficients<double>::makeHighPass(sr,scHpFreq.load());
    *scHpL.coefficients=*c; *scHpR.coefficients=*c;
}

void CompressorStage::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S3_Comp_On","Comp On",true));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S3_Comp_Model","Model",juce::StringArray{"VCA","Opto","FET","Vari-Mu"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Threshold","Threshold",juce::NormalisableRange<float>(-60,0,0.1f),-20));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Ratio","Ratio",juce::NormalisableRange<float>(1,20,0.1f,0.5f),4));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Attack","Attack",juce::NormalisableRange<float>(0.1f,100,0.1f,0.4f),10));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Release","Release",juce::NormalisableRange<float>(10,1000,1,0.4f),100));
    layout.add(std::make_unique<juce::AudioParameterBool>("S3_Comp_AutoRelease","Auto Release",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Makeup","Makeup",juce::NormalisableRange<float>(0,24,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Mix","Mix",juce::NormalisableRange<float>(0,100,1),100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_SC_HP","SC HP",juce::NormalisableRange<float>(20,500,1,0.4f),20));
}

void CompressorStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S3_Comp_On")->load()>0.5f);
    model.store((int)a.getRawParameterValue("S3_Comp_Model")->load());
    threshold.store(a.getRawParameterValue("S3_Comp_Threshold")->load());
    ratio.store(a.getRawParameterValue("S3_Comp_Ratio")->load());
    attackMs.store(a.getRawParameterValue("S3_Comp_Attack")->load());
    releaseMs.store(a.getRawParameterValue("S3_Comp_Release")->load());
    autoRelease.store(a.getRawParameterValue("S3_Comp_AutoRelease")->load()>0.5f);
    makeupGain.store(a.getRawParameterValue("S3_Comp_Makeup")->load());
    mix.store(a.getRawParameterValue("S3_Comp_Mix")->load());
    scHpFreq.store(a.getRawParameterValue("S3_Comp_SC_HP")->load());
    updateCoefficients();
}

// ─────────────────────────────────────────────────────────────
//  SATURATION STAGE
// ─────────────────────────────────────────────────────────────

void SaturationStage::prepare (double sr, int bs)
{
    currentSampleRate=sr; currentBlockSize=bs;
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)bs,2};
    xover1LP.prepare(spec); xover1HP.prepare(spec);
    xover2LP.prepare(spec); xover2HP.prepare(spec);
    xover3LP.prepare(spec); xover3HP.prepare(spec);
    for (auto& b:bandBuffers) b.setSize(2,bs);
}

void SaturationStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters(block);
    int n=(int)block.getNumSamples();

    if (mode.load()==0)
    {
        double drv=juce::Decibels::decibelsToGain((double)drive.load());
        double out=juce::Decibels::decibelsToGain((double)output.load());
        double bld=blend.load()/100.0; int st=satType.load();
        for (int ch=0;ch<2;++ch)
        {
            auto* d=block.getChannelPointer(ch);
            for (int i=0;i<n;++i)
            { double dry=d[i]; d[i]=dry*(1-bld)+saturateSample(d[i],st,drv)*out*bld; }
        }
    }
    else
    {
        for (auto& b:bandBuffers) b.setSize(2,n,false,false,true);
        for (int ch=0;ch<2;++ch)
        { auto* s=block.getChannelPointer(ch);
          for (int b=0;b<4;++b) juce::FloatVectorOperations::copy(bandBuffers[b].getWritePointer(ch),s,n); }

        bool anySolo=false;
        for (int b=0;b<4;++b) if(bandParams[b].solo.load()) anySolo=true;
        block.clear();

        for (int b=0;b<4;++b)
        {
            if(bandParams[b].mute.load()) continue;
            if(anySolo&&!bandParams[b].solo.load()) continue;
            double drv=juce::Decibels::decibelsToGain((double)bandParams[b].drive.load());
            double out=juce::Decibels::decibelsToGain((double)bandParams[b].output.load());
            double bld=bandParams[b].blend.load()/100.0; int typ=bandParams[b].type.load();
            for (int ch=0;ch<2;++ch)
            {
                auto* bd=bandBuffers[b].getWritePointer(ch);
                auto* dst=block.getChannelPointer(ch);
                for (int i=0;i<n;++i)
                { double dry=bd[i]; dst[i]+=dry*(1-bld)+saturateSample(bd[i],typ,drv)*out*bld; }
            }
        }
    }
    updateOutputMeters(block);
}

void SaturationStage::reset()
{ xover1LP.reset();xover1HP.reset();xover2LP.reset();xover2HP.reset();xover3LP.reset();xover3HP.reset(); }

double SaturationStage::saturateSample (double input, int type, double driveLinear)
{
    double x=input*driveLinear;
    switch(type)
    {
        case 0: return std::tanh(x);
        case 1: return x>=0 ? 1-std::exp(-x) : -(1-std::exp(x))*0.8;
        case 2: return x/(1+std::abs(x));
        case 3: return juce::jlimit(-1.0,1.0,x);
        case 4: { double lv=std::pow(2.0,bits.load()); return std::round(x*lv)/lv; }
        default: return std::tanh(x);
    }
}

void SaturationStage::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S4_Sat_On","Sat On",true));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S4_Sat_Mode","Mode",juce::StringArray{"Single","Multiband"},0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S4_Sat_Type","Type",juce::StringArray{"Tape","Tube","Transistor","Digital","Bitcrush"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_Drive","Drive",juce::NormalisableRange<float>(0,24,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_Bits","Bits",juce::NormalisableRange<float>(4,24,1),16));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_Rate","Rate",juce::NormalisableRange<float>(1000,48000,1),44100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_Output","Output",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_Blend","Blend",juce::NormalisableRange<float>(0,100,1),100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_Xover1","Xover1",juce::NormalisableRange<float>(20,500,1,0.4f),120));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_Xover2","Xover2",juce::NormalisableRange<float>(200,5000,1,0.35f),1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_Xover3","Xover3",juce::NormalisableRange<float>(1000,16000,1,0.3f),5000));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S4_Sat_Xover_Mode","XMode",juce::StringArray{"Min Phase","Linear Phase"},0));
    for (int b=1;b<=4;++b)
    {
        auto p="S4_Sat_B"+juce::String(b)+"_"; auto lb="B"+juce::String(b)+" ";
        layout.add(std::make_unique<juce::AudioParameterChoice>(p+"Type",lb+"Type",juce::StringArray{"Tape","Tube","Transistor","Digital","Bitcrush"},0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p+"Drive",lb+"Drive",juce::NormalisableRange<float>(0,24,0.1f),0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p+"Bits",lb+"Bits",juce::NormalisableRange<float>(4,24,1),16));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p+"Rate",lb+"Rate",juce::NormalisableRange<float>(1000,48000,1),44100));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p+"Output",lb+"Out",juce::NormalisableRange<float>(-12,12,0.1f),0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p+"Blend",lb+"Blend",juce::NormalisableRange<float>(0,100,1),100));
        layout.add(std::make_unique<juce::AudioParameterBool>(p+"Solo",lb+"Solo",false));
        layout.add(std::make_unique<juce::AudioParameterBool>(p+"Mute",lb+"Mute",false));
    }
}

void SaturationStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S4_Sat_On")->load()>0.5f);
    mode.store((int)a.getRawParameterValue("S4_Sat_Mode")->load());
    satType.store((int)a.getRawParameterValue("S4_Sat_Type")->load());
    drive.store(a.getRawParameterValue("S4_Sat_Drive")->load());
    bits.store(a.getRawParameterValue("S4_Sat_Bits")->load());
    rate.store(a.getRawParameterValue("S4_Sat_Rate")->load());
    output.store(a.getRawParameterValue("S4_Sat_Output")->load());
    blend.store(a.getRawParameterValue("S4_Sat_Blend")->load());
    xoverFreq1.store(a.getRawParameterValue("S4_Sat_Xover1")->load());
    xoverFreq2.store(a.getRawParameterValue("S4_Sat_Xover2")->load());
    xoverFreq3.store(a.getRawParameterValue("S4_Sat_Xover3")->load());
    xoverMode.store((int)a.getRawParameterValue("S4_Sat_Xover_Mode")->load());
    for (int b=0;b<4;++b)
    {
        auto p="S4_Sat_B"+juce::String(b+1)+"_";
        bandParams[b].type.store((int)a.getRawParameterValue(p+"Type")->load());
        bandParams[b].drive.store(a.getRawParameterValue(p+"Drive")->load());
        bandParams[b].bits.store(a.getRawParameterValue(p+"Bits")->load());
        bandParams[b].rate.store(a.getRawParameterValue(p+"Rate")->load());
        bandParams[b].output.store(a.getRawParameterValue(p+"Output")->load());
        bandParams[b].blend.store(a.getRawParameterValue(p+"Blend")->load());
        bandParams[b].solo.store(a.getRawParameterValue(p+"Solo")->load()>0.5f);
        bandParams[b].mute.store(a.getRawParameterValue(p+"Mute")->load()>0.5f);
    }
}

// ─────────────────────────────────────────────────────────────
//  OUTPUT EQ STAGE
// ─────────────────────────────────────────────────────────────

void OutputEQStage::prepare(double sr,int bs)
{
    currentSampleRate=sr; currentBlockSize=bs;
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)bs,1};
    hsL.prepare(spec);hsR.prepare(spec);lsL.prepare(spec);lsR.prepare(spec);midL.prepare(spec);midR.prepare(spec);
    updateFilters();
}

void OutputEQStage::process(juce::dsp::AudioBlock<double>& block)
{
    if(!stageOn.load())return; updateInputMeters(block);
    int n=(int)block.getNumSamples(); auto*l=block.getChannelPointer(0); auto*r=block.getChannelPointer(1);
    for(int i=0;i<n;++i)
    { l[i]=hsL.processSample(lsL.processSample(midL.processSample(l[i])));
      r[i]=hsR.processSample(lsR.processSample(midR.processSample(r[i]))); }
    updateOutputMeters(block);
}

void OutputEQStage::reset(){hsL.reset();hsR.reset();lsL.reset();lsR.reset();midL.reset();midR.reset();}

void OutputEQStage::updateFilters()
{
    double sr=currentSampleRate; if(sr<=0)return;
    auto h=juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr,hsFreq.load(),0.707,juce::Decibels::decibelsToGain((double)hsGain.load()));
    *hsL.coefficients=*h;*hsR.coefficients=*h;
    auto lo=juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr,lsFreq.load(),0.707,juce::Decibels::decibelsToGain((double)lsGain.load()));
    *lsL.coefficients=*lo;*lsR.coefficients=*lo;
    auto m=juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr,midFreq.load(),1.0,juce::Decibels::decibelsToGain((double)midGain.load()));
    *midL.coefficients=*m;*midR.coefficients=*m;
}

void OutputEQStage::addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S5_EQ2_On","OutEQ On",true));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_HighShelf_Freq","HS F",juce::NormalisableRange<float>(1000,16000,1,0.3f),8000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_HighShelf_Gain","HS G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_LowShelf_Freq","LS F",juce::NormalisableRange<float>(20,500,1,0.4f),100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_LowShelf_Gain","LS G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_Mid_Freq","Mid F",juce::NormalisableRange<float>(100,10000,1,0.3f),1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_Mid_Gain","Mid G",juce::NormalisableRange<float>(-12,12,0.1f),0));
}

void OutputEQStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S5_EQ2_On")->load()>0.5f);
    hsFreq.store(a.getRawParameterValue("S5_EQ2_HighShelf_Freq")->load());
    hsGain.store(a.getRawParameterValue("S5_EQ2_HighShelf_Gain")->load());
    lsFreq.store(a.getRawParameterValue("S5_EQ2_LowShelf_Freq")->load());
    lsGain.store(a.getRawParameterValue("S5_EQ2_LowShelf_Gain")->load());
    midFreq.store(a.getRawParameterValue("S5_EQ2_Mid_Freq")->load());
    midGain.store(a.getRawParameterValue("S5_EQ2_Mid_Gain")->load());
    updateFilters();
}

// ─────────────────────────────────────────────────────────────
//  FILTER STAGE
// ─────────────────────────────────────────────────────────────

void FilterStage::prepare(double sr,int bs)
{
    currentSampleRate=sr;currentBlockSize=bs;
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)bs,1};
    for(int i=0;i<MAX_STAGES;++i){hpL[i].prepare(spec);hpR[i].prepare(spec);lpL[i].prepare(spec);lpR[i].prepare(spec);}
    updateFilters();
}

void FilterStage::process(juce::dsp::AudioBlock<double>& block)
{
    if(!stageOn.load())return; updateInputMeters(block);
    int n=(int)block.getNumSamples(); auto*l=block.getChannelPointer(0); auto*r=block.getChannelPointer(1);
    int hpS=hpOn.load()?(hpSlope.load()==4?4:hpSlope.load()+1):0;
    int lpS=lpOn.load()?(lpSlope.load()==4?4:lpSlope.load()+1):0;
    for(int i=0;i<n;++i)
    {
        double sL=l[i],sR=r[i];
        for(int s=0;s<hpS&&s<MAX_STAGES;++s){sL=hpL[s].processSample(sL);sR=hpR[s].processSample(sR);}
        for(int s=0;s<lpS&&s<MAX_STAGES;++s){sL=lpL[s].processSample(sL);sR=lpR[s].processSample(sR);}
        l[i]=sL; r[i]=sR;
    }
    updateOutputMeters(block);
}

void FilterStage::reset(){for(int i=0;i<MAX_STAGES;++i){hpL[i].reset();hpR[i].reset();lpL[i].reset();lpR[i].reset();}}
int FilterStage::getLatencySamples()const{return filterMode.load()==1?256:0;}

void FilterStage::updateFilters()
{
    double sr=currentSampleRate;if(sr<=0)return;
    for(int i=0;i<MAX_STAGES;++i)
    {
        auto h=juce::dsp::IIR::Coefficients<double>::makeHighPass(sr,hpFreq.load(),0.707);
        *hpL[i].coefficients=*h;*hpR[i].coefficients=*h;
        auto lo=juce::dsp::IIR::Coefficients<double>::makeLowPass(sr,lpFreq.load(),0.707);
        *lpL[i].coefficients=*lo;*lpR[i].coefficients=*lo;
    }
}

void FilterStage::addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S6_Filter_On","Filter On",true));
    layout.add(std::make_unique<juce::AudioParameterBool>("S6_HP_On","HP On",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6_HP_Freq","HP F",juce::NormalisableRange<float>(10,500,1,0.4f),30));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6_HP_Slope","HP Slope",juce::StringArray{"6","12","18","24","48"},1));
    layout.add(std::make_unique<juce::AudioParameterBool>("S6_LP_On","LP On",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6_LP_Freq","LP F",juce::NormalisableRange<float>(1000,20000,1,0.3f),18000));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6_LP_Slope","LP Slope",juce::StringArray{"6","12","18","24","48"},1));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6_Filter_Mode","Flt Mode",juce::StringArray{"Min Phase","Linear Phase"},0));
}

void FilterStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S6_Filter_On")->load()>0.5f);
    hpOn.store(a.getRawParameterValue("S6_HP_On")->load()>0.5f);
    hpFreq.store(a.getRawParameterValue("S6_HP_Freq")->load());
    hpSlope.store((int)a.getRawParameterValue("S6_HP_Slope")->load());
    lpOn.store(a.getRawParameterValue("S6_LP_On")->load()>0.5f);
    lpFreq.store(a.getRawParameterValue("S6_LP_Freq")->load());
    lpSlope.store((int)a.getRawParameterValue("S6_LP_Slope")->load());
    filterMode.store((int)a.getRawParameterValue("S6_Filter_Mode")->load());
    updateFilters();
}

// ─────────────────────────────────────────────────────────────
//  DYNAMIC RESONANCE STAGE
// ─────────────────────────────────────────────────────────────

void DynamicResonanceStage::prepare(double sr,int bs)
{
    currentSampleRate=sr;currentBlockSize=bs;
    double freqs[]={800,1200,2000,3000,4000,5500,7500,10000};
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)bs,1};
    for(int i=0;i<NUM_BANDS;++i)
    {
        bands[i].centerFreq=freqs[i]; bands[i].envelope=0;
        bands[i].filterL.prepare(spec); bands[i].filterR.prepare(spec);
        auto c=juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr,freqs[i],4.0,1.0);
        *bands[i].filterL.coefficients=*c; *bands[i].filterR.coefficients=*c;
    }
    attackCoeff=std::exp(-1.0/(sr*0.001)); releaseCoeff=std::exp(-1.0/(sr*0.050));
}

void DynamicResonanceStage::process(juce::dsp::AudioBlock<double>& block)
{
    if(!stageOn.load())return; updateInputMeters(block);
    int n=(int)block.getNumSamples(); auto*l=block.getChannelPointer(0); auto*r=block.getChannelPointer(1);
    double depthAmt=depth.load()/100.0, sensAmt=sensitivity.load()/100.0;
    double threshDb=-20+(1-sensAmt)*30;
    if(depthAmt<0.001)return;

    for(int i=0;i<n;++i)
    {
        double mono=(l[i]+r[i])*0.5;
        double inLvl=juce::Decibels::gainToDecibels(std::abs(mono),-100.0);
        for(auto&band:bands)
        {
            if(inLvl>band.envelope) band.envelope=attackCoeff*band.envelope+(1-attackCoeff)*inLvl;
            else band.envelope=releaseCoeff*band.envelope+(1-releaseCoeff)*inLvl;
            double cutGain=1.0;
            if(band.envelope>threshDb)
            { double over=band.envelope-threshDb; cutGain=juce::Decibels::decibelsToGain(-over*depthAmt*0.5); }
            auto c=juce::dsp::IIR::Coefficients<double>::makePeakFilter(currentSampleRate,band.centerFreq,4.0,cutGain);
            *band.filterL.coefficients=*c; *band.filterR.coefficients=*c;
            l[i]=band.filterL.processSample(l[i]); r[i]=band.filterR.processSample(r[i]);
        }
    }
    updateOutputMeters(block);
}

void DynamicResonanceStage::reset()
{ for(auto&b:bands){b.filterL.reset();b.filterR.reset();b.envelope=0;} }

void DynamicResonanceStage::addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S6B_DynEQ_On","DynRes On",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_Depth","Depth",juce::NormalisableRange<float>(0,100,1),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_Sensitivity","Sens",juce::NormalisableRange<float>(0,100,1),50));
}

void DynamicResonanceStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S6B_DynEQ_On")->load()>0.5f);
    depth.store(a.getRawParameterValue("S6B_DynEQ_Depth")->load());
    sensitivity.store(a.getRawParameterValue("S6B_DynEQ_Sensitivity")->load());
}

// ─────────────────────────────────────────────────────────────
//  CLIPPER STAGE
// ─────────────────────────────────────────────────────────────

void ClipperStage::prepare(double sr,int bs){currentSampleRate=sr;currentBlockSize=bs;}

void ClipperStage::process(juce::dsp::AudioBlock<double>& block)
{
    if(!stageOn.load())return; updateInputMeters(block);
    double cl=juce::Decibels::decibelsToGain((double)ceiling.load()); int st=style.load();
    int n=(int)block.getNumSamples();
    for(int ch=0;ch<(int)block.getNumChannels();++ch)
    { auto*d=block.getChannelPointer(ch); for(int i=0;i<n;++i) d[i]=clipSample(d[i],cl,st); }
    updateOutputMeters(block);
}

void ClipperStage::reset(){}

double ClipperStage::clipSample(double input,double cl,int st)
{
    switch(st)
    {
        case 0: return juce::jlimit(-cl,cl,input);
        case 1: return std::tanh(input/cl)*cl;
        case 2: { double nm=input/cl; return nm>=0?(1-std::exp(-nm))*cl:-(1-std::exp(nm))*cl*0.85; }
        default: return juce::jlimit(-cl,cl,input);
    }
}

void ClipperStage::addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S7_Clipper_On","Clipper On",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S7_Clipper_Ceiling","Clip Ceil",juce::NormalisableRange<float>(-6,0,0.1f),-0.3f));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S7_Clipper_Style","Clip Style",juce::StringArray{"Hard","Soft","Analog"},0));
}

void ClipperStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S7_Clipper_On")->load()>0.5f);
    ceiling.store(a.getRawParameterValue("S7_Clipper_Ceiling")->load());
    style.store((int)a.getRawParameterValue("S7_Clipper_Style")->load());
}

// ─────────────────────────────────────────────────────────────
//  LIMITER STAGE
// ─────────────────────────────────────────────────────────────

void LimiterStage::prepare(double sr,int bs)
{
    currentSampleRate=sr;currentBlockSize=bs;
    int maxDel=(int)(sr*0.005)+1;
    delayBuffer.setSize(2,maxDel+bs); delayBuffer.clear(); delayWritePos=0;
    lookaheadSamples=(int)(sr*lookaheadMs.load()/1000.0);
    grEnvelope=1.0; truePeakHistory.fill(0); tpHistoryPos=0;
    attackCoeff=std::exp(-1.0/(sr*0.0001));
    releaseCoeff=std::exp(-1.0/(sr*releaseMs.load()/1000.0));
}

void LimiterStage::process(juce::dsp::AudioBlock<double>& block)
{
    if(!stageOn.load())return; updateInputMeters(block);
    int n=(int)block.getNumSamples(), nch=(int)block.getNumChannels();
    int delSz=delayBuffer.getNumSamples();
    double inG=juce::Decibels::decibelsToGain((double)inputGain.load());
    double ceil=juce::Decibels::decibelsToGain((double)ceilingDb.load());
    int limSt=style.load();

    if(std::abs(inG-1.0)>1e-6) block.multiplyBy(inG);

    for(int i=0;i<n;++i)
    {
        for(int ch=0;ch<nch&&ch<2;++ch)
            delayBuffer.setSample(ch,(delayWritePos+i)%delSz,block.getSample(ch,i));

        double peak=0;
        for(int ch=0;ch<nch&&ch<2;++ch)
            peak=std::max(peak,detectTruePeak(block.getSample(ch,i)));

        double tg=computeGain(peak,ceil);
        if(tg<grEnvelope)
        {
            double ac=attackCoeff; if(limSt==1) ac=std::exp(-1.0/(currentSampleRate*0.00005));
            grEnvelope=ac*grEnvelope+(1-ac)*tg;
        }
        else
        {
            double rc=releaseCoeff;
            if(autoRelease.load())
            {
                double grDb=juce::Decibels::gainToDecibels(grEnvelope,-100.0);
                double ms=juce::jlimit(20.0,500.0,juce::jmap(grDb,-12.0,0.0,200.0,50.0));
                rc=std::exp(-1.0/(currentSampleRate*ms/1000.0));
            }
            if(limSt==2) rc=std::pow(rc,0.7);
            grEnvelope=rc*grEnvelope+(1-rc)*tg;
        }

        int rIdx=(delayWritePos+i-lookaheadSamples+delSz)%delSz;
        for(int ch=0;ch<nch&&ch<2;++ch)
            block.setSample(ch,i,delayBuffer.getSample(ch,rIdx)*grEnvelope);

        meterData.gainReduction.store((float)juce::Decibels::gainToDecibels(grEnvelope,-100.0));
    }
    delayWritePos=(delayWritePos+n)%delSz;
    updateOutputMeters(block);
}

void LimiterStage::reset(){delayBuffer.clear();delayWritePos=0;grEnvelope=1.0;truePeakHistory.fill(0);tpHistoryPos=0;}
int LimiterStage::getLatencySamples()const{return lookaheadSamples;}

double LimiterStage::detectTruePeak(double sample)
{
    truePeakHistory[tpHistoryPos]=sample; tpHistoryPos=(tpHistoryPos+1)%(int)truePeakHistory.size();
    double mx=std::abs(sample);
    int prev=(tpHistoryPos-2+(int)truePeakHistory.size())%(int)truePeakHistory.size();
    double s0=truePeakHistory[prev],s1=sample;
    for(int k=1;k<=3;++k){double t=k/4.0; mx=std::max(mx,std::abs(s0+(s1-s0)*t));}
    return mx;
}

double LimiterStage::computeGain(double peak,double ceil)
{ return (peak<=ceil||peak<1e-10)?1.0:ceil/peak; }

void LimiterStage::addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S7_Lim_On","Limiter On",true));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S7_Lim_Input","Lim Input",juce::NormalisableRange<float>(0,24,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S7_Lim_Ceiling","Lim Ceiling",juce::NormalisableRange<float>(-3,0,0.1f),-0.3f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S7_Lim_Release","Lim Release",juce::NormalisableRange<float>(10,500,1,0.5f),100));
    layout.add(std::make_unique<juce::AudioParameterBool>("S7_Lim_AutoRelease","Auto Rel",true));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S7_Lim_Lookahead","Lookahead",juce::NormalisableRange<float>(0.1f,5,0.1f),1));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S7_Lim_Style","Lim Style",juce::StringArray{"Transparent","Aggressive","Warm"},0));
}

void LimiterStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S7_Lim_On")->load()>0.5f);
    inputGain.store(a.getRawParameterValue("S7_Lim_Input")->load());
    ceilingDb.store(a.getRawParameterValue("S7_Lim_Ceiling")->load());
    releaseMs.store(a.getRawParameterValue("S7_Lim_Release")->load());
    autoRelease.store(a.getRawParameterValue("S7_Lim_AutoRelease")->load()>0.5f);
    style.store((int)a.getRawParameterValue("S7_Lim_Style")->load());
    float nl=a.getRawParameterValue("S7_Lim_Lookahead")->load();
    if(std::abs(nl-lookaheadMs.load())>0.01f)
    { lookaheadMs.store(nl); lookaheadSamples=(int)(currentSampleRate*nl/1000.0); }
    if(currentSampleRate>0) releaseCoeff=std::exp(-1.0/(currentSampleRate*releaseMs.load()/1000.0));
}

// ─────────────────────────────────────────────────────────────
//  OVERSAMPLING ENGINE
// ─────────────────────────────────────────────────────────────

void OversamplingEngine::prepare(double sr,int bs,int nCh,int factor)
{
    currentFactor=juce::jlimit(1,8,factor);
    if(currentFactor<=1){oversampler.reset();prepared=false;return;}
    int order=0;int f=currentFactor;while(f>1){f>>=1;++order;}
    oversampler=std::make_unique<juce::dsp::Oversampling<double>>(nCh,order,
        juce::dsp::Oversampling<double>::filterHalfBandPolyphaseIIR,true);
    oversampler->initProcessing(bs); prepared=true;
}

juce::dsp::AudioBlock<double> OversamplingEngine::upsample(juce::dsp::AudioBlock<double>& in)
{ if(!prepared||!oversampler)return in; return oversampler->processSamplesUp(in); }

void OversamplingEngine::downsample(juce::dsp::AudioBlock<double>&,juce::dsp::AudioBlock<double>& out)
{ if(!prepared||!oversampler)return; oversampler->processSamplesDown(out); }

void OversamplingEngine::reset(){if(oversampler)oversampler->reset();}
int OversamplingEngine::getLatency()const{return (!prepared||!oversampler)?0:(int)oversampler->getLatencyInSamples();}

// ─────────────────────────────────────────────────────────────
//  LUFS METER
// ─────────────────────────────────────────────────────────────

void LUFSMeter::prepare(double sr,int bs)
{
    sampleRate=sr;blockSize=bs;
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)bs,1};
    preFilterL.prepare(spec);preFilterR.prepare(spec);rlbFilterL.prepare(spec);rlbFilterR.prepare(spec);
    auto pre=juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr,1681,0.7,juce::Decibels::decibelsToGain(4.0f));
    *preFilterL.coefficients=*pre;*preFilterR.coefficients=*pre;
    auto rlb=juce::dsp::IIR::Coefficients<float>::makeHighPass(sr,38,0.5);
    *rlbFilterL.coefficients=*rlb;*rlbFilterR.coefficients=*rlb;
    blockPowers.clear();momentaryPower=0;momentaryBlockCount=0;
}

void LUFSMeter::process(const juce::AudioBuffer<float>& buf)
{
    int n=buf.getNumSamples(); if(buf.getNumChannels()<2||n==0)return;
    auto*rL=buf.getReadPointer(0);auto*rR=buf.getReadPointer(1);
    float pL=0,pR=0;
    for(int i=0;i<n;++i){pL=std::max(pL,std::abs(rL[i]));pR=std::max(pR,std::abs(rR[i]));}
    float pDb=juce::Decibels::gainToDecibels(std::max(pL,pR),-100.f);
    if(pDb>truePeak.load())truePeak.store(pDb);

    double sum=0;
    for(int i=0;i<n;++i)
    { float kL=rlbFilterL.processSample(preFilterL.processSample(rL[i]));
      float kR=rlbFilterR.processSample(preFilterR.processSample(rR[i]));
      sum+=(double)(kL*kL+kR*kR); }
    momentaryPower+=sum/(double)n; momentaryBlockCount++;

    int b400=std::max(1,(int)(sampleRate*0.4/blockSize));
    if(momentaryBlockCount>=b400)
    {
        double avg=momentaryPower/momentaryBlockCount;
        momentaryLUFS.store((float)(-0.691+10*std::log10(std::max(avg,1e-10))));
        blockPowers.push_back(avg); momentaryPower=0; momentaryBlockCount=0;
    }
    if(!blockPowers.empty())
    {
        double s=0;int c=0;
        for(auto p:blockPowers){double l=-0.691+10*std::log10(std::max(p,1e-10));if(l>-70){s+=p;++c;}}
        if(c>0)
        {
            double rg=-0.691+10*std::log10(s/c)-10;
            double fs=0;int fc=0;
            for(auto p:blockPowers){double l=-0.691+10*std::log10(std::max(p,1e-10));if(l>rg){fs+=p;++fc;}}
            if(fc>0) integratedLUFS.store((float)(-0.691+10*std::log10(fs/fc)));
        }
    }
}

void LUFSMeter::reset()
{
    preFilterL.reset();preFilterR.reset();rlbFilterL.reset();rlbFilterR.reset();
    blockPowers.clear();momentaryPower=0;momentaryBlockCount=0;
    integratedLUFS.store(-100.f);momentaryLUFS.store(-100.f);truePeak.store(-100.f);
}

// ─────────────────────────────────────────────────────────────
//  PROCESSING ENGINE
// ─────────────────────────────────────────────────────────────

ProcessingEngine::ProcessingEngine()
{
    inputStage=std::make_unique<InputStage>();
    limiterStage=std::make_unique<LimiterStage>();
    reorderableStages[0]=std::make_unique<PultecEQStage>();
    reorderableStages[1]=std::make_unique<CompressorStage>();
    reorderableStages[2]=std::make_unique<SaturationStage>();
    reorderableStages[3]=std::make_unique<OutputEQStage>();
    reorderableStages[4]=std::make_unique<FilterStage>();
    reorderableStages[5]=std::make_unique<DynamicResonanceStage>();
    reorderableStages[6]=std::make_unique<ClipperStage>();
    resetStageOrder();
    oversamplingEngine=std::make_unique<OversamplingEngine>();
    lufsMeter=std::make_unique<LUFSMeter>();
}

void ProcessingEngine::prepare(double sr,int bs)
{
    currentSampleRate=sr;currentBlockSize=bs;
    oversamplingEngine->prepare(sr,bs,2,oversamplingFactor.load());
    double eSR=sr*oversamplingFactor.load(); int eBs=bs*oversamplingFactor.load();
    inputStage->prepare(eSR,eBs);
    for(auto&s:reorderableStages)s->prepare(eSR,eBs);
    limiterStage->prepare(eSR,eBs);
    lufsMeter->prepare(sr,bs);
    doubleBuffer.setSize(2,eBs);
}

void ProcessingEngine::process(juce::AudioBuffer<float>& buffer)
{
    int nch=buffer.getNumChannels(),n=buffer.getNumSamples();
    if(nch<2||n==0)return;
    doubleBuffer.setSize(nch,n,false,false,true);
    for(int ch=0;ch<nch;++ch)
    { auto*s=buffer.getReadPointer(ch);auto*d=doubleBuffer.getWritePointer(ch);
      for(int i=0;i<n;++i) d[i]=(double)s[i]; }

    juce::dsp::AudioBlock<double> block(doubleBuffer);
    auto osBlock=oversamplingEngine->upsample(block);

    inputStage->process(osBlock);
    for(int p=0;p<NUM_REORDERABLE;++p)
    {
        int idx=stageOrder[p].load(std::memory_order_relaxed);
        auto&stage=reorderableStages[idx];
        if(stage->isEnabled()) stage->process(osBlock);
    }
    limiterStage->process(osBlock);

    oversamplingEngine->downsample(osBlock,block);

    double g=masterOutputGain.load(std::memory_order_relaxed);
    if(std::abs(g-1.0)>1e-6) block.multiplyBy(g);

    for(int ch=0;ch<nch;++ch)
    { auto*s=doubleBuffer.getReadPointer(ch);auto*d=buffer.getWritePointer(ch);
      for(int i=0;i<n;++i) d[i]=(float)s[i]; }

    lufsMeter->process(buffer);
}

void ProcessingEngine::reset()
{
    inputStage->reset();
    for(auto&s:reorderableStages)s->reset();
    limiterStage->reset();oversamplingEngine->reset();lufsMeter->reset();
}

juce::AudioProcessorValueTreeState::ParameterLayout ProcessingEngine::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterBool>("Global_Bypass","Global Bypass",false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Auto_Match","Auto Match",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Master_Output_Gain","Master Output",juce::NormalisableRange<float>(-12,12,0.1f),0,juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterChoice>("Oversampling","Oversampling",juce::StringArray{"Off","2x","4x","8x"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("LUFS_Target","LUFS Target",juce::NormalisableRange<float>(-24,-6,0.1f),-14,juce::AudioParameterFloatAttributes().withLabel("LUFS")));
    inputStage->addParameters(layout);
    for(auto&s:reorderableStages)s->addParameters(layout);
    limiterStage->addParameters(layout);
    return layout;
}

void ProcessingEngine::updateAllParameters(const juce::AudioProcessorValueTreeState& a)
{
    masterOutputGain.store(juce::Decibels::decibelsToGain((double)a.getRawParameterValue("Master_Output_Gain")->load()));
    int oc=(int)a.getRawParameterValue("Oversampling")->load();
    int nf=1<<oc;
    if(nf!=oversamplingFactor.load()){oversamplingFactor.store(nf);prepare(currentSampleRate,currentBlockSize);}
    inputStage->updateParameters(a);
    for(auto&s:reorderableStages)s->updateParameters(a);
    limiterStage->updateParameters(a);
}

std::array<int,ProcessingEngine::NUM_REORDERABLE> ProcessingEngine::getStageOrder()const
{ std::array<int,NUM_REORDERABLE> o; for(int i=0;i<NUM_REORDERABLE;++i)o[i]=stageOrder[i].load(std::memory_order_relaxed); return o; }

void ProcessingEngine::setStageOrder(const std::array<int,NUM_REORDERABLE>& newOrder)
{
    std::array<bool,NUM_REORDERABLE> seen{};
    for(int idx:newOrder) if(idx<0||idx>=NUM_REORDERABLE||seen[idx])return; else seen[idx]=true;
    juce::SpinLock::ScopedLockType lock(orderLock);
    for(int i=0;i<NUM_REORDERABLE;++i) stageOrder[i].store(newOrder[i],std::memory_order_relaxed);
}

void ProcessingEngine::swapStages(int a,int b)
{
    if(a<0||a>=NUM_REORDERABLE||b<0||b>=NUM_REORDERABLE)return;
    juce::SpinLock::ScopedLockType lock(orderLock);
    int va=stageOrder[a].load(),vb=stageOrder[b].load();
    stageOrder[a].store(vb);stageOrder[b].store(va);
}

void ProcessingEngine::moveStage(int from,int to)
{
    if(from<0||from>=NUM_REORDERABLE||to<0||to>=NUM_REORDERABLE||from==to)return;
    juce::SpinLock::ScopedLockType lock(orderLock);
    std::array<int,NUM_REORDERABLE> o;
    for(int i=0;i<NUM_REORDERABLE;++i)o[i]=stageOrder[i].load();
    int mv=o[from];
    if(from<to) for(int i=from;i<to;++i)o[i]=o[i+1];
    else for(int i=from;i>to;--i)o[i]=o[i-1];
    o[to]=mv;
    for(int i=0;i<NUM_REORDERABLE;++i)stageOrder[i].store(o[i]);
}

void ProcessingEngine::resetStageOrder()
{for(int i=0;i<NUM_REORDERABLE;++i)stageOrder[i].store(i);}

ProcessingStage* ProcessingEngine::getStage(ProcessingStage::StageID id)
{
    if(id==ProcessingStage::StageID::Input)return inputStage.get();
    if(id==ProcessingStage::StageID::Limiter)return limiterStage.get();
    for(auto&s:reorderableStages)if(s->getStageID()==id)return s.get();
    return nullptr;
}

ProcessingStage* ProcessingEngine::getStageAtPosition(int pos)
{
    if(pos==0)return inputStage.get();
    if(pos==NUM_REORDERABLE+1)return limiterStage.get();
    if(pos>=1&&pos<=NUM_REORDERABLE)return reorderableStages[stageOrder[pos-1].load()].get();
    return nullptr;
}

int ProcessingEngine::getTotalLatency()const
{
    int t=inputStage->getLatencySamples();
    for(auto&s:reorderableStages)t+=s->getLatencySamples();
    t+=limiterStage->getLatencySamples();
    t+=oversamplingEngine->getLatency();
    return t;
}

float ProcessingEngine::getLUFS()const{return lufsMeter->getIntegratedLUFS();}
float ProcessingEngine::getTruePeak()const{return lufsMeter->getTruePeak();}

// ─────────────────────────────────────────────────────────────
//  PRESET MANAGER
// ─────────────────────────────────────────────────────────────

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& s):apvts(s)
{ getUserPresetsFolder().createDirectory(); }

juce::File PresetManager::getUserPresetsFolder()const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Phonica School").getChildFile("Easy Master").getChildFile("Presets");
}

void PresetManager::savePreset(const juce::String& name)
{
    auto state=apvts.copyState(); auto xml=state.createXml(); if(!xml)return;
    juce::String os;
    for(int i=0;i<7;++i){if(i>0)os+=",";os+=juce::String(stageOrder[i]);}
    xml->setAttribute("stageOrder",os); xml->setAttribute("presetName",name);
    xml->writeTo(getUserPresetsFolder().getChildFile(name+".xml"));
    currentPreset=name;
}

bool PresetManager::loadPreset(const juce::String& name)
{
    auto f=getUserPresetsFolder().getChildFile(name+".xml");
    if(!f.existsAsFile())return false;
    auto xml=juce::XmlDocument::parse(f); if(!xml)return false;
    auto os=xml->getStringAttribute("stageOrder","0,1,2,3,4,5,6");
    auto tok=juce::StringArray::fromTokens(os,",","");
    if(tok.size()==7) for(int i=0;i<7;++i)stageOrder[i]=tok[i].getIntValue();
    auto tree=juce::ValueTree::fromXml(*xml);
    if(tree.isValid())apvts.replaceState(tree);
    currentPreset=name; return true;
}

void PresetManager::loadInit()
{
    for(auto*p:apvts.processor.getParameters())
        if(auto*r=dynamic_cast<juce::RangedAudioParameter*>(p))
            r->setValueNotifyingHost(r->getDefaultValue());
    stageOrder={0,1,2,3,4,5,6}; currentPreset="INIT";
}

juce::StringArray PresetManager::getPresetList()const
{
    juce::StringArray list; list.add("INIT");
    auto dir=getUserPresetsFolder();
    if(dir.isDirectory())
        for(auto&f:dir.findChildFiles(juce::File::findFiles,false,"*.xml"))
            list.add(f.getFileNameWithoutExtension());
    return list;
}

// ─────────────────────────────────────────────────────────────
//  LICENSE MANAGER
// ─────────────────────────────────────────────────────────────

LicenseManager::LicenseManager(){loadStoredLicense();}

juce::File LicenseManager::getLicenseFile()const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Phonica School").getChildFile("Easy Master").getChildFile(".license");
}

bool LicenseManager::validateFormat(const juce::String& s)const
{
    if(!s.startsWith("PHONICA-"))return false;
    auto p=juce::StringArray::fromTokens(s,"-","");
    if(p.size()!=4||p[0]!="PHONICA")return false;
    for(int i=1;i<4;++i){if(p[i].length()!=4)return false;
        for(auto c:p[i])if(!juce::CharacterFunctions::isLetterOrDigit(c))return false;}
    return true;
}

bool LicenseManager::activate(const juce::String& serial)
{
    auto t=serial.trim().toUpperCase();
    if(!validateFormat(t))return false;
    currentSerial=t;activated=true;saveLicense();return true;
}

void LicenseManager::deactivate(){activated=false;currentSerial.clear();getLicenseFile().deleteFile();}

void LicenseManager::loadStoredLicense()
{
    auto f=getLicenseFile(); if(!f.existsAsFile())return;
    auto c=f.loadFileAsString().trim();
    if(validateFormat(c)){currentSerial=c;activated=true;}
}

void LicenseManager::saveLicense()
{
    auto f=getLicenseFile();f.getParentDirectory().createDirectory();
    f.replaceWithText(currentSerial);
}

// ─────────────────────────────────────────────────────────────
//  PLUGIN PROCESSOR
// ─────────────────────────────────────────────────────────────

EasyMasterProcessor::EasyMasterProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
      apvts(*this,nullptr,"EasyMasterState",engine.createParameterLayout()),
      presetManager(apvts)
{}

EasyMasterProcessor::~EasyMasterProcessor()=default;

void EasyMasterProcessor::prepareToPlay(double sr,int bs)
{ engine.prepare(sr,bs); engine.updateAllParameters(apvts); setLatencySamples(engine.getTotalLatency()); }

void EasyMasterProcessor::releaseResources(){engine.reset();}

void EasyMasterProcessor::processBlock(juce::AudioBuffer<float>& buf,juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    for(auto i=getTotalNumInputChannels();i<getTotalNumOutputChannels();++i)
        buf.clear(i,0,buf.getNumSamples());

    // Global bypass check
    bool bypassed = apvts.getRawParameterValue("Global_Bypass")->load() > 0.5f;
    bool autoMatch = apvts.getRawParameterValue("Auto_Match")->load() > 0.5f;

    if (bypassed)
        return;  // Pass-through: audio untouched

    // Measure input RMS for auto-match
    float inputRms = 0.0f;
    if (autoMatch)
    {
        int n = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            auto* data = buf.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
                inputRms += data[i] * data[i];
        }
        inputRms = std::sqrt(inputRms / (float)(n * buf.getNumChannels()));
    }

    engine.updateAllParameters(apvts);
    setLatencySamples(engine.getTotalLatency());
    engine.process(buf);

    // Auto-match: compensate output level to match input
    if (autoMatch && inputRms > 1e-6f)
    {
        int n = buf.getNumSamples();
        float outputRms = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            auto* data = buf.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
                outputRms += data[i] * data[i];
        }
        outputRms = std::sqrt(outputRms / (float)(n * buf.getNumChannels()));

        if (outputRms > 1e-6f)
        {
            float compensation = inputRms / outputRms;
            compensation = juce::jlimit(0.1f, 10.0f, compensation);
            // Smooth the compensation
            smoothedMatchGain = smoothedMatchGain * 0.95f + compensation * 0.05f;
            buf.applyGain(smoothedMatchGain);
        }
    }
}

juce::AudioProcessorEditor* EasyMasterProcessor::createEditor(){return new EasyMasterEditor(*this);}

void EasyMasterProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state=apvts.copyState();
    juce::String os; auto order=engine.getStageOrder();
    for(int i=0;i<ProcessingEngine::NUM_REORDERABLE;++i){if(i>0)os+=",";os+=juce::String(order[i]);}
    state.setProperty("stageOrder",os,nullptr);
    auto xml=state.createXml(); if(xml)copyXmlToBinary(*xml,dest);
}

void EasyMasterProcessor::setStateInformation(const void* data,int size)
{
    auto xml=getXmlFromBinary(data,size);
    if(!xml)return;
    auto tree=juce::ValueTree::fromXml(*xml);
    if(!tree.isValid())return;
    apvts.replaceState(tree);
    auto os=tree.getProperty("stageOrder","0,1,2,3,4,5,6").toString();
    auto tok=juce::StringArray::fromTokens(os,",","");
    if(tok.size()==ProcessingEngine::NUM_REORDERABLE)
    {
        std::array<int,ProcessingEngine::NUM_REORDERABLE> o;
        for(int i=0;i<ProcessingEngine::NUM_REORDERABLE;++i)o[i]=tok[i].getIntValue();
        engine.setStageOrder(o);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new EasyMasterProcessor();}
