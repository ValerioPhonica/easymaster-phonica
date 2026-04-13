// ═══════════════════════════════════════════════════════════════
//  EASY MASTER — Phonica School
//  All implementations consolidated
// ═══════════════════════════════════════════════════════════════

#include "PluginProcessor.h"

// ─────────────────────────────────────────────────────────────
//  LINEAR PHASE FIR UTILITY — Pure windowed-sinc (bulletproof)
// ─────────────────────────────────────────────────────────────

double LinearPhaseFIR::besselI0 (double x)
{
    // Modified Bessel function of the first kind, order 0
    // Series: I0(x) = sum_{k=0}^{inf} [(x/2)^k / k!]^2
    double sum = 1.0, term = 1.0;
    for (int k = 1; k <= 25; ++k)
    {
        term *= (x * 0.5) / (double) k;
        sum += term * term;
    }
    return sum;
}

void LinearPhaseFIR::prepare (double sampleRate, int maxBlockSize)
{
    int queueCap = FIR_SIZE * 3;
    kernelFreqA.assign ((size_t)(FFT_SIZE * 2), 0.0f);
    kernelFreqB.assign ((size_t)(FFT_SIZE * 2), 0.0f);
    kernelTime.assign ((size_t) FIR_SIZE, 0.0);

    auto initCh = [&] (ChannelConv& ch) {
        ch.inputRing.assign ((size_t) FIR_SIZE, 0.0f);
        ch.fftWorkspace.assign ((size_t)(FFT_SIZE * 2), 0.0f);
        ch.outputQueue.assign ((size_t) queueCap, 0.0f);
        ch.writePos = 0;
        ch.outputReadPos = 0;
        ch.outputWritePos = FIR_SIZE / 2; // pre-fill zeros = latency
        ch.outputAvail = FIR_SIZE / 2;
    };
    initCh (convL); initCh (convR); initCh (convMono);

    activeKernel.store (0);
    newKernelReady.store (false);
    active = false;
    prepared = true;
}

void LinearPhaseFIR::processChannel (ChannelConv& ch, double* inOut, int numSamples)
{
    auto* kf = (activeKernel.load() == 0) ? kernelFreqA.data() : kernelFreqB.data();
    int queueCap = (int) ch.outputQueue.size();

    for (int i = 0; i < numSamples; ++i)
    {
        // Store input sample
        ch.inputRing[(size_t) ch.writePos] = (float) inOut[i];
        ch.writePos++;

        // When we have FIR_SIZE new input samples, run FFT convolution
        if (ch.writePos >= FIR_SIZE)
        {
            auto& fb = ch.fftWorkspace;

            // 1. Copy input block + zero-pad to FFT_SIZE
            for (int j = 0; j < FIR_SIZE; ++j)
                fb[(size_t) j] = ch.inputRing[(size_t) j];
            for (size_t j = (size_t) FIR_SIZE; j < fb.size(); ++j)
                fb[j] = 0.0f;

            // 2. Forward real FFT
            fftEngine.performRealOnlyForwardTransform (fb.data());

            // 3. Complex multiply with kernel spectrum
            //    JUCE packs as [re0, im0, re1, im1, ..., reN/2, imN/2]
            //    Total meaningful values: FFT_SIZE + 2
            for (int j = 0; j <= FFT_SIZE; j += 2)
            {
                float re = fb[(size_t) j]     * kf[j]     - fb[(size_t)(j+1)] * kf[j+1];
                float im = fb[(size_t) j]     * kf[j+1]   + fb[(size_t)(j+1)] * kf[j];
                fb[(size_t) j]     = re;
                fb[(size_t)(j+1)] = im;
            }

            // 4. Inverse FFT
            fftEngine.performRealOnlyInverseTransform (fb.data());

            // 5. Overlap-add: accumulate FFT_SIZE result samples into output queue
            for (int j = 0; j < FFT_SIZE; ++j)
            {
                int pos = (ch.outputWritePos + j) % queueCap;
                ch.outputQueue[(size_t) pos] += fb[(size_t) j];
            }

            // 6. Advance write position by FIR_SIZE (one block of new output is ready)
            ch.outputWritePos = (ch.outputWritePos + FIR_SIZE) % queueCap;
            ch.outputAvail += FIR_SIZE;
            ch.writePos = 0;
        }

        // Read output sample (pre-seeded with zeros for latency)
        if (ch.outputAvail > 0)
        {
            inOut[i] = (double) ch.outputQueue[(size_t) ch.outputReadPos];
            ch.outputQueue[(size_t) ch.outputReadPos] = 0.0f; // clear for next overlap-add cycle
            ch.outputReadPos = (ch.outputReadPos + 1) % queueCap;
            ch.outputAvail--;
        }
        else
        {
            inOut[i] = 0.0; // underrun (shouldn't happen after prepare)
        }
    }
}

void LinearPhaseFIR::designLowpass (double cutoffHz, double sampleRate, int slopeDb)
{
    if (!prepared || sampleRate <= 0 || cutoffHz <= 0) { active = false; return; }

    // Build the SAME IIR cascade that FilterStage uses → guaranteed identical magnitude
    static const int stageMap[] = { 1, 1, 2, 2, 4 };
    int slopeIdx = (slopeDb <= 6) ? 0 : (slopeDb <= 12) ? 1 : (slopeDb <= 18) ? 2 : (slopeDb <= 24) ? 3 : 4;
    int numStages = stageMap[slopeIdx];

    std::vector<juce::dsp::IIR::Coefficients<double>::Ptr> iirC;
    for (int s = 0; s < numStages; ++s)
        iirC.push_back ((slopeIdx == 0 && s == 0)
            ? juce::dsp::IIR::Coefficients<double>::makeFirstOrderLowPass (sampleRate, cutoffHz)
            : juce::dsp::IIR::Coefficients<double>::makeLowPass (sampleRate, cutoffHz, 0.707));

    // Sample IIR magnitude
    int N = FIR_SIZE, halfN = N / 2;
    std::vector<double> mag ((size_t)(halfN + 1));
    for (int k = 0; k <= halfN; ++k)
    {
        double freq = (double) k * sampleRate / (double) N;
        double m = 1.0;
        for (auto& c : iirC) m *= c->getMagnitudeForFrequency (juce::jmax (freq, 0.1), sampleRate);
        mag[(size_t) k] = m;
    }

    // IFFT → zero-phase kernel (O(N·logN) instead of O(N²) cosine synthesis)
    // Pack magnitude as real-only spectrum (zero phase = symmetric kernel)
    std::vector<float> fftBuf ((size_t)(N * 2), 0.0f);
    for (int k = 0; k <= halfN; ++k)
    {
        fftBuf[(size_t)(k * 2)]     = (float) mag[(size_t) k]; // real
        fftBuf[(size_t)(k * 2 + 1)] = 0.0f;                     // imag = 0 (zero phase)
    }
    designFft.performRealOnlyInverseTransform (fftBuf.data());

    // Circular shift by N/2: IFFT output is centered at 0, we need it at N/2
    std::vector<double> kern ((size_t) N, 0.0);
    for (int i = 0; i < N; ++i)
    {
        int srcIdx = (i + halfN) % N;
        kern[(size_t) i] = (double) fftBuf[(size_t) srcIdx];
    }

    // Normalize DC = 1.0
    double dcSum = 0;
    for (auto k : kern) dcSum += k;
    if (std::abs (dcSum) > 1e-15)
        for (auto& k : kern) k /= dcSum;

    // Store time-domain kernel for display
    kernelTime.resize ((size_t) N);
    for (int i = 0; i < N; ++i) kernelTime[(size_t) i] = kern[(size_t) i];

    // FFT the kernel → frequency domain for fast convolution
    int wrIdx = 1 - activeKernel.load();
    auto& kfBuf = (wrIdx == 0) ? kernelFreqA : kernelFreqB;
    kfBuf.assign ((size_t)(FFT_SIZE * 2), 0.0f);
    for (int i = 0; i < N; ++i) kfBuf[(size_t) i] = (float) kern[(size_t) i];
    fftEngine.performRealOnlyForwardTransform (kfBuf.data());

    newKernelReady.store (true);
    active = true;
}

void LinearPhaseFIR::designHighpass (double cutoffHz, double sampleRate, int slopeDb)
{
    if (!prepared || sampleRate <= 0 || cutoffHz <= 0) { active = false; return; }

    static const int stageMap[] = { 1, 1, 2, 2, 4 };
    int slopeIdx = (slopeDb <= 6) ? 0 : (slopeDb <= 12) ? 1 : (slopeDb <= 18) ? 2 : (slopeDb <= 24) ? 3 : 4;
    int numStages = stageMap[slopeIdx];

    std::vector<juce::dsp::IIR::Coefficients<double>::Ptr> iirC;
    for (int s = 0; s < numStages; ++s)
        iirC.push_back ((slopeIdx == 0 && s == 0)
            ? juce::dsp::IIR::Coefficients<double>::makeFirstOrderHighPass (sampleRate, cutoffHz)
            : juce::dsp::IIR::Coefficients<double>::makeHighPass (sampleRate, cutoffHz, 0.707));

    int N = FIR_SIZE, halfN = N / 2;
    std::vector<double> mag ((size_t)(halfN + 1));
    for (int k = 0; k <= halfN; ++k)
    {
        double freq = (double) k * sampleRate / (double) N;
        double m = 1.0;
        for (auto& c : iirC) m *= c->getMagnitudeForFrequency (juce::jmax (freq, 0.1), sampleRate);
        mag[(size_t) k] = m;
    }
    mag[0] = 0.0;

    // IFFT → zero-phase kernel (O(N·logN) instead of O(N²))
    std::vector<float> fftBuf ((size_t)(N * 2), 0.0f);
    for (int k = 0; k <= halfN; ++k)
    {
        fftBuf[(size_t)(k * 2)]     = (float) mag[(size_t) k];
        fftBuf[(size_t)(k * 2 + 1)] = 0.0f;
    }
    designFft.performRealOnlyInverseTransform (fftBuf.data());

    std::vector<double> kern ((size_t) N, 0.0);
    for (int i = 0; i < N; ++i)
    {
        int srcIdx = (i + halfN) % N;
        kern[(size_t) i] = (double) fftBuf[(size_t) srcIdx];
    }

    // Normalize passband = 1.0 (at Nyquist)
    double nyG = 0;
    for (int i = 0; i < N; ++i)
        nyG += kern[(size_t) i] * ((i % 2 == 0) ? 1.0 : -1.0);
    if (std::abs (nyG) > 1e-15)
        for (auto& k : kern) k /= std::abs (nyG);

    kernelTime.resize ((size_t) N);
    for (int i = 0; i < N; ++i) kernelTime[(size_t) i] = kern[(size_t) i];

    int wrIdx = 1 - activeKernel.load();
    auto& kfBuf = (wrIdx == 0) ? kernelFreqA : kernelFreqB;
    kfBuf.assign ((size_t)(FFT_SIZE * 2), 0.0f);
    for (int i = 0; i < N; ++i) kfBuf[(size_t) i] = (float) kern[(size_t) i];
    fftEngine.performRealOnlyForwardTransform (kfBuf.data());

    newKernelReady.store (true);
    active = true;
}

void LinearPhaseFIR::process (juce::dsp::AudioBlock<double>& block)
{
    if (!active || block.getNumChannels() < 2) return;
    if (newKernelReady.load()) {
        activeKernel.store (1 - activeKernel.load());
        newKernelReady.store (false);
    }
    auto* l = block.getChannelPointer (0);
    auto* r = block.getChannelPointer (1);
    int n = (int) block.getNumSamples();
    processChannel (convL, l, n);
    processChannel (convR, r, n);
}

void LinearPhaseFIR::processMono (double* data, int numSamples)
{
    if (!active) return;
    if (newKernelReady.load()) {
        activeKernel.store (1 - activeKernel.load());
        newKernelReady.store (false);
    }
    processChannel (convMono, data, numSamples);
}

double LinearPhaseFIR::getMagnitudeAtFreq (double freq, double sampleRate) const
{
    if (!active || sampleRate <= 0 || kernelTime.empty()) return 1.0;
    double w = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
    double re = 0, im = 0;
    int N = (int) kernelTime.size();
    for (int i = 0; i < N; ++i)
    {
        re += kernelTime[(size_t) i] * std::cos (w * i);
        im -= kernelTime[(size_t) i] * std::sin (w * i);
    }
    return std::sqrt (re * re + im * im);
}

void LinearPhaseFIR::reset()
{
    auto resetCh = [] (ChannelConv& ch) {
        std::fill (ch.inputRing.begin(), ch.inputRing.end(), 0.0f);
        std::fill (ch.outputQueue.begin(), ch.outputQueue.end(), 0.0f);
        std::fill (ch.fftWorkspace.begin(), ch.fftWorkspace.end(), 0.0f);
        ch.writePos = 0;
        ch.outputReadPos = 0;
        ch.outputWritePos = 0;
        ch.outputAvail = 0;
    };
    resetCh (convL); resetCh (convR); resetCh (convMono);
    active = false;
    newKernelReady.store (false);
}

// ─────────────────────────────────────────────────────────────
//  INPUT STAGE
// ─────────────────────────────────────────────────────────────

void InputStage::prepare (double sr, int bs)
{
    currentSampleRate = sr; currentBlockSize = bs;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 1 };
    dcBlockL.prepare (spec); dcBlockR.prepare (spec);
    auto dcCoeffs = juce::dsp::IIR::Coefficients<double>::makeHighPass (sr, 5.0);
    *dcBlockL.coefficients = *dcCoeffs;
    *dcBlockR.coefficients = *dcCoeffs;
}

void InputStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (! stageOn.load()) return;
    updateInputMeters (block);
    const int n = (int)block.getNumSamples();
    auto* l = block.getChannelPointer(0);
    auto* r = block.getChannelPointer(1);

    // Input gain
    double gain = inputGain.load (std::memory_order_relaxed);
    if (std::abs(gain - 1.0) > 1e-6) block.multiplyBy (gain);

    // DC offset removal
    if (dcFilter.load())
    {
        for (int i = 0; i < n; ++i)
        {
            l[i] = dcBlockL.processSample (l[i]);
            r[i] = dcBlockR.processSample (r[i]);
        }
    }

    // Phase invert
    bool invL = phaseInvertL.load(), invR = phaseInvertR.load();
    if (invL || invR)
    {
        double mL = invL ? -1.0 : 1.0;
        double mR = invR ? -1.0 : 1.0;
        for (int i = 0; i < n; ++i) { l[i] *= mL; r[i] *= mR; }
    }

    // Stereo balance
    float bal = balance.load();
    if (std::abs (bal) > 0.5f)
    {
        // -100 = full left, +100 = full right
        double balNorm = bal / 100.0;
        double gainL = (balNorm <= 0) ? 1.0 : 1.0 - balNorm;
        double gainR = (balNorm >= 0) ? 1.0 : 1.0 + balNorm;
        for (int i = 0; i < n; ++i) { l[i] *= gainL; r[i] *= gainR; }
    }

    // M/S trim
    double mg = midGain.load(), sg = sideGain.load();
    if (std::abs(mg - 1.0) > 1e-6 || std::abs(sg - 1.0) > 1e-6)
    {
        for (int i = 0; i < n; ++i)
        {
            double mid  = (l[i] + r[i]) * 0.5;
            double side = (l[i] - r[i]) * 0.5;
            mid *= mg; side *= sg;
            l[i] = mid + side;
            r[i] = mid - side;
        }
    }

    // Mono check
    if (monoCheck.load())
    {
        for (int i = 0; i < n; ++i)
        {
            double mono = (l[i] + r[i]) * 0.5;
            l[i] = mono; r[i] = mono;
        }
    }

    // Correlation
    double sLR=0, sLL=0, sRR=0;
    for (int i=0;i<n;++i) { sLR+=l[i]*r[i]; sLL+=l[i]*l[i]; sRR+=r[i]*r[i]; }
    double d=std::sqrt(sLL*sRR);
    correlation.store(d>1e-12 ? (float)(sLR/d) : 1.0f);

    updateOutputMeters (block);
}

void InputStage::reset() { dcBlockL.reset(); dcBlockR.reset(); }

int InputStage::getLatencySamples() const { return 0; }

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
    layout.add(std::make_unique<juce::AudioParameterBool>("S1_Input_DC","DC Filter",false));
    layout.add(std::make_unique<juce::AudioParameterBool>("S1_Input_PhaseL","Phase Inv L",false));
    layout.add(std::make_unique<juce::AudioParameterBool>("S1_Input_PhaseR","Phase Inv R",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S1_Input_Balance","Balance",juce::NormalisableRange<float>(-100,100,1),0));
    layout.add(std::make_unique<juce::AudioParameterBool>("S1_Input_Mono","Mono Check",false));
}

void InputStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S1_Input_On")->load()>0.5f);
    inputGain.store(juce::Decibels::decibelsToGain((double)a.getRawParameterValue("S1_Input_Gain")->load()));
    midGain.store(juce::Decibels::decibelsToGain((double)a.getRawParameterValue("S1_Input_Mid_Gain")->load()));
    sideGain.store(juce::Decibels::decibelsToGain((double)a.getRawParameterValue("S1_Input_Side_Gain")->load()));
    dcFilter.store(a.getRawParameterValue("S1_Input_DC")->load()>0.5f);
    phaseInvertL.store(a.getRawParameterValue("S1_Input_PhaseL")->load()>0.5f);
    phaseInvertR.store(a.getRawParameterValue("S1_Input_PhaseR")->load()>0.5f);
    balance.store(a.getRawParameterValue("S1_Input_Balance")->load());
    monoCheck.store(a.getRawParameterValue("S1_Input_Mono")->load()>0.5f);
}

// ─────────────────────────────────────────────────────────────
//  PULTEC EQ STAGE
// ─────────────────────────────────────────────────────────────

void PultecEQStage::prepare (double sr, int bs)
{
    currentSampleRate = sr; currentBlockSize = bs;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 1 };
    // Stereo filters
    lowShelfL.prepare(spec); lowShelfR.prepare(spec);
    lowResonanceL.prepare(spec); lowResonanceR.prepare(spec);
    lowAttenL.prepare(spec); lowAttenR.prepare(spec);
    highPeakL.prepare(spec); highPeakR.prepare(spec);
    highAirL.prepare(spec); highAirR.prepare(spec);
    highAttenL.prepare(spec); highAttenR.prepare(spec);
    lowMidL.prepare(spec); lowMidR.prepare(spec);
    lowMidSkirtL.prepare(spec); lowMidSkirtR.prepare(spec);
    midDipL.prepare(spec); midDipR.prepare(spec);
    midDipSkirtL.prepare(spec); midDipSkirtR.prepare(spec);
    highMidL.prepare(spec); highMidR.prepare(spec);
    highMidSkirtL.prepare(spec); highMidSkirtR.prepare(spec);
    xfmrL.prepare(spec); xfmrR.prepare(spec);
    // Mid filters (mono)
    midLowShelf.prepare(spec); midLowResonance.prepare(spec); midLowAtten.prepare(spec);
    midHighPeak.prepare(spec); midHighAir.prepare(spec); midHighAtten.prepare(spec);
    midLowMid.prepare(spec); midLowMidSkirt.prepare(spec);
    midMidDip.prepare(spec); midMidDipSkirt.prepare(spec);
    midHighMid.prepare(spec); midHighMidSkirt.prepare(spec);
    midXfmr.prepare(spec);
    // Side filters (mono)
    sideLowShelf.prepare(spec); sideLowResonance.prepare(spec); sideLowAtten.prepare(spec);
    sideHighPeak.prepare(spec); sideHighAir.prepare(spec); sideHighAtten.prepare(spec);
    sideLowMid.prepare(spec); sideLowMidSkirt.prepare(spec);
    sideMidDip.prepare(spec); sideMidDipSkirt.prepare(spec);
    sideHighMid.prepare(spec); sideHighMidSkirt.prepare(spec);
    sideXfmr.prepare(spec);
    updateFilters();
    updateMidFilters();
    updateSideFilters();
}

void PultecEQStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters (block);
    int n = (int) block.getNumSamples();
    int ms = msMode.load(); // 0=Stereo, 1=Mid, 2=Side

    if (ms > 0)
    {
        // ─── M/S DUAL: independent Mid + Side processing ───
        auto* ch0 = block.getChannelPointer (0);
        auto* ch1 = block.getChannelPointer (1);

        // Encode L/R → M/S
        for (int i = 0; i < n; ++i)
        {
            double mid  = (ch0[i] + ch1[i]) * 0.5;
            double side = (ch0[i] - ch1[i]) * 0.5;
            ch0[i] = mid; ch1[i] = side;
        }

        // Process Mid channel (ch0) with Mid filters
        for (int i = 0; i < n; ++i)
        {
            double s = ch0[i];
            s = midLowShelf.processSample (s);
            s = midLowResonance.processSample (s);
            s = midLowAtten.processSample (s);
            s = midHighPeak.processSample (s);
            s = midHighAir.processSample (s);
            s = midHighAtten.processSample (s);
            s = midLowMid.processSample (s);
            s = midLowMidSkirt.processSample (s);
            s = midMidDip.processSample (s);
            s = midMidDipSkirt.processSample (s);
            s = midHighMid.processSample (s);
            s = midHighMidSkirt.processSample (s);
            s = tubeSaturate (s);
            s = midXfmr.processSample (s);
            ch0[i] = s;
        }

        // Process Side channel (ch1) with Side filters
        for (int i = 0; i < n; ++i)
        {
            double s = ch1[i];
            s = sideLowShelf.processSample (s);
            s = sideLowResonance.processSample (s);
            s = sideLowAtten.processSample (s);
            s = sideHighPeak.processSample (s);
            s = sideHighAir.processSample (s);
            s = sideHighAtten.processSample (s);
            s = sideLowMid.processSample (s);
            s = sideLowMidSkirt.processSample (s);
            s = sideMidDip.processSample (s);
            s = sideMidDipSkirt.processSample (s);
            s = sideHighMid.processSample (s);
            s = sideHighMidSkirt.processSample (s);
            s = tubeSaturate (s);
            s = sideXfmr.processSample (s);
            ch1[i] = s;
        }

        // Decode M/S → L/R
        for (int i = 0; i < n; ++i)
        {
            double m = ch0[i], side = ch1[i];
            ch0[i] = m + side;
            ch1[i] = m - side;
        }
    }
    else
    {
        // Stereo: process both channels
        auto* l = block.getChannelPointer (0);
        auto* r = block.getChannelPointer (1);

        for (int i = 0; i < n; ++i)
        {
            double L = l[i], R = r[i];
            L = lowShelfL.processSample (L);         R = lowShelfR.processSample (R);
            L = lowResonanceL.processSample (L);     R = lowResonanceR.processSample (R);
            L = lowAttenL.processSample (L);         R = lowAttenR.processSample (R);
            L = highPeakL.processSample (L);         R = highPeakR.processSample (R);
            L = highAirL.processSample (L);          R = highAirR.processSample (R);
            L = highAttenL.processSample (L);        R = highAttenR.processSample (R);
            L = lowMidL.processSample (L);           R = lowMidR.processSample (R);
            L = lowMidSkirtL.processSample (L);      R = lowMidSkirtR.processSample (R);
            L = midDipL.processSample (L);           R = midDipR.processSample (R);
            L = midDipSkirtL.processSample (L);      R = midDipSkirtR.processSample (R);
            L = highMidL.processSample (L);          R = highMidR.processSample (R);
            L = highMidSkirtL.processSample (L);     R = highMidSkirtR.processSample (R);
            L = tubeSaturate (L);                    R = tubeSaturate (R);
            L = xfmrL.processSample (L);            R = xfmrR.processSample (R);
            l[i] = L; r[i] = R;
        }
    }

    // FFT on output
    auto* outL = block.getChannelPointer (0);
    auto* outR = block.getChannelPointer (1);
    for (int i = 0; i < n; ++i)
        pushSampleToFFT ((float)((outL[i] + outR[i]) * 0.5));

    updateOutputMeters (block);
}

double PultecEQStage::tubeSaturate (double x) const
{
    // 12AX7 triode model — asymmetric waveshaping
    // Even harmonics dominant (2nd ~3%, 3rd ~0.8%)
    // Soft clipping with tube-like warmth
    double drive = 1.1;  // very subtle — Pultec is clean, not distorted
    double xd = x * drive;

    // Asymmetric: positive half clips softer (plate saturation)
    // Negative half clips harder (grid cutoff)
    double y;
    if (xd >= 0.0)
        y = 1.0 - std::exp (-xd);                    // soft positive
    else
        y = -(1.0 - std::exp (xd * 0.85)) * 0.95;   // slightly harder negative

    // Blend: mostly clean, subtle tube coloring
    // At unity gain this adds ~1.5% THD (2nd harmonic dominant)
    return x * 0.92 + y * 0.08;
}

void PultecEQStage::reset()
{
    lowShelfL.reset(); lowShelfR.reset();
    lowResonanceL.reset(); lowResonanceR.reset();
    lowAttenL.reset(); lowAttenR.reset();
    highPeakL.reset(); highPeakR.reset();
    highAirL.reset(); highAirR.reset();
    highAttenL.reset(); highAttenR.reset();
    lowMidL.reset(); lowMidR.reset();
    lowMidSkirtL.reset(); lowMidSkirtR.reset();
    midDipL.reset(); midDipR.reset();
    midDipSkirtL.reset(); midDipSkirtR.reset();
    highMidL.reset(); highMidR.reset();
    highMidSkirtL.reset(); highMidSkirtR.reset();
    xfmrL.reset(); xfmrR.reset();
    // Mid filters
    midLowShelf.reset(); midLowResonance.reset(); midLowAtten.reset();
    midHighPeak.reset(); midHighAir.reset(); midHighAtten.reset();
    midLowMid.reset(); midLowMidSkirt.reset();
    midMidDip.reset(); midMidDipSkirt.reset();
    midHighMid.reset(); midHighMidSkirt.reset();
    midXfmr.reset();
    // Side filters
    sideLowShelf.reset(); sideLowResonance.reset(); sideLowAtten.reset();
    sideHighPeak.reset(); sideHighAir.reset(); sideHighAtten.reset();
    sideLowMid.reset(); sideLowMidSkirt.reset();
    sideMidDip.reset(); sideMidDipSkirt.reset();
    sideHighMid.reset(); sideHighMidSkirt.reset();
    sideXfmr.reset();
    fftFifoIndex = 0; fftReady.store (false);
}

double PultecEQStage::getMagnitudeAtFreq (double freq) const
{
    double sr = currentSampleRate;
    if (sr <= 0) return 0.0;
    double mag = 1.0;
    if (lowShelfL.coefficients)     mag *= lowShelfL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (lowResonanceL.coefficients) mag *= lowResonanceL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (lowAttenL.coefficients)     mag *= lowAttenL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (highPeakL.coefficients)     mag *= highPeakL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (highAirL.coefficients)      mag *= highAirL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (highAttenL.coefficients)    mag *= highAttenL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (lowMidL.coefficients)       mag *= lowMidL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (lowMidSkirtL.coefficients)  mag *= lowMidSkirtL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midDipL.coefficients)       mag *= midDipL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midDipSkirtL.coefficients)  mag *= midDipSkirtL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (highMidL.coefficients)      mag *= highMidL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (highMidSkirtL.coefficients) mag *= highMidSkirtL.coefficients->getMagnitudeForFrequency (freq, sr);
    if (xfmrL.coefficients)         mag *= xfmrL.coefficients->getMagnitudeForFrequency (freq, sr);
    return juce::Decibels::gainToDecibels (mag, -60.0);
}

double PultecEQStage::getMagnitudeAtFreqMid (double freq) const
{
    double sr = currentSampleRate;
    if (sr <= 0) return 0.0;
    double mag = 1.0;
    if (midLowShelf.coefficients)     mag *= midLowShelf.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midLowResonance.coefficients) mag *= midLowResonance.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midLowAtten.coefficients)     mag *= midLowAtten.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midHighPeak.coefficients)     mag *= midHighPeak.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midHighAir.coefficients)      mag *= midHighAir.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midHighAtten.coefficients)    mag *= midHighAtten.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midLowMid.coefficients)       mag *= midLowMid.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midLowMidSkirt.coefficients)  mag *= midLowMidSkirt.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midMidDip.coefficients)       mag *= midMidDip.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midMidDipSkirt.coefficients)  mag *= midMidDipSkirt.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midHighMid.coefficients)      mag *= midHighMid.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midHighMidSkirt.coefficients) mag *= midHighMidSkirt.coefficients->getMagnitudeForFrequency (freq, sr);
    if (midXfmr.coefficients)         mag *= midXfmr.coefficients->getMagnitudeForFrequency (freq, sr);
    return juce::Decibels::gainToDecibels (mag, -60.0);
}

double PultecEQStage::getMagnitudeAtFreqSide (double freq) const
{
    double sr = currentSampleRate;
    if (sr <= 0) return 0.0;
    double mag = 1.0;
    if (sideLowShelf.coefficients)     mag *= sideLowShelf.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideLowResonance.coefficients) mag *= sideLowResonance.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideLowAtten.coefficients)     mag *= sideLowAtten.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideHighPeak.coefficients)     mag *= sideHighPeak.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideHighAir.coefficients)      mag *= sideHighAir.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideHighAtten.coefficients)    mag *= sideHighAtten.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideLowMid.coefficients)       mag *= sideLowMid.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideLowMidSkirt.coefficients)  mag *= sideLowMidSkirt.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideMidDip.coefficients)       mag *= sideMidDip.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideMidDipSkirt.coefficients)  mag *= sideMidDipSkirt.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideHighMid.coefficients)      mag *= sideHighMid.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideHighMidSkirt.coefficients) mag *= sideHighMidSkirt.coefficients->getMagnitudeForFrequency (freq, sr);
    if (sideXfmr.coefficients)         mag *= sideXfmr.coefficients->getMagnitudeForFrequency (freq, sr);
    return juce::Decibels::gainToDecibels (mag, -60.0);
}

void PultecEQStage::pushSampleToFFT (float sample)
{
    fftFifo[(size_t) fftFifoIndex] = sample;
    if (++fftFifoIndex >= fftSize)
    {
        fftFifoIndex = 0;
        std::copy (fftFifo.begin(), fftFifo.end(), fftData.begin());
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fftReady.store (true, std::memory_order_release);
    }
}

void PultecEQStage::computeFFTMagnitudes()
{
    if (! fftReady.load (std::memory_order_acquire)) return;
    fftReady.store (false, std::memory_order_release);
    fftWindow.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fftProcessor.performFrequencyOnlyForwardTransform (fftData.data());
    for (int i = 0; i < fftSize / 2; ++i)
    {
        auto level = juce::Decibels::gainToDecibels (fftData[(size_t)i] / (float) fftSize, -80.0f);
        fftMagnitudes[(size_t)i] = juce::jmap (level, -80.0f, 0.0f, 0.0f, 1.0f);
    }
}

void PultecEQStage::updateFilters()
{
    double sr = currentSampleRate; if (sr <= 0) return;

    // ════════════════════════════════════════════════════════
    //  EQP-1A LOW SECTION — Circuit model
    //
    //  Real Pultec low boost uses an LC network (inductor + capacitor).
    //  The inductor creates a resonance peak just ABOVE the shelf frequency.
    //  This is what gives the Pultec its "tight, punchy" low end —
    //  not a muddy shelf, but a boost with definition.
    //
    //  Model: Low Shelf (main boost) + Peak (inductor overshoot at ~1.5x freq)
    //  Atten: Narrower shelf at same freq (higher Q = faster rolloff)
    //  Combined: boost at freq, dip above → the "Pultec trick"
    // ════════════════════════════════════════════════════════

    double lowF = lowBoostFreq.load();
    double lbAmount = lowBoostGain.load();  // 0-10 knob
    double laAmount = lowAttenGain.load();

    // Scale: 0-10 knob → 0-12 dB, with gentle log taper like real pot
    double lbDb = lbAmount * 1.2;
    double laDb = laAmount * 1.2;

    // Main low shelf boost — wide Q (like inductor-loaded passive circuit)
    auto lsCoeff = juce::dsp::IIR::Coefficients<double>::makeLowShelf (
        sr, lowF, 0.55, juce::Decibels::decibelsToGain (lbDb));
    *lowShelfL.coefficients = *lsCoeff; *lowShelfR.coefficients = *lsCoeff;

    // Inductor overshoot resonance — peak at ~1.4x the shelf frequency
    // This is the KEY to the Pultec sound. The inductor in the passive circuit
    // creates a resonance that tightens the bass boost.
    // Gain = ~35% of the boost, Q = ~1.8 (medium-narrow)
    double overshootFreq = lowF * 1.4;
    double overshootDb = lbDb * 0.35;
    auto lrCoeff = juce::dsp::IIR::Coefficients<double>::makePeakFilter (
        sr, overshootFreq, 1.8, juce::Decibels::decibelsToGain (overshootDb));
    *lowResonanceL.coefficients = *lrCoeff; *lowResonanceR.coefficients = *lrCoeff;

    // Low atten — narrower shelf (Q=1.4) at same frequency
    // Different Q from boost creates the asymmetry:
    //   below freq: boost wins (wide) → net boost
    //   at freq: both compete → partial cancellation
    //   above freq: atten wins (narrow, extends higher) → net cut (the "dip")
    auto laCoeff = juce::dsp::IIR::Coefficients<double>::makeLowShelf (
        sr, lowF, 1.4, juce::Decibels::decibelsToGain (-laDb));
    *lowAttenL.coefficients = *laCoeff; *lowAttenR.coefficients = *laCoeff;

    // ════════════════════════════════════════════════════════
    //  EQP-1A HIGH SECTION — Circuit model
    //
    //  High boost: LC resonant peak (bell shape, NOT a shelf!)
    //  The inductor creates the characteristic "air" — a bell boost
    //  with a slight extension above the peak frequency.
    //  Bandwidth knob controls the inductor damping → Q of the peak.
    //
    //  High atten: Simple RC shelf at a separately selected frequency.
    // ════════════════════════════════════════════════════════

    double highF = highBoostFreq.load();
    double hbAmount = highBoostGain.load();
    double haAmount = highAttenGain.load();
    double bwKnob = highAttenBW.load();   // 0=sharp, 10=broad
    double haFreq = highAttenFreq.load();

    double hbDb = hbAmount * 1.2;
    double haDb = haAmount * 1.2;

    // Bandwidth → Q mapping (real Pultec pot taper)
    // Sharp (0) = Q ~4.5, Broad (10) = Q ~0.35
    double highQ = 4.5 * std::exp (-bwKnob * 0.26);

    // Main high boost — resonant peak (bell)
    auto hpCoeff = juce::dsp::IIR::Coefficients<double>::makePeakFilter (
        sr, highF, highQ, juce::Decibels::decibelsToGain (hbDb));
    *highPeakL.coefficients = *hpCoeff; *highPeakR.coefficients = *hpCoeff;

    // "Air" shelf — subtle boost above the peak frequency
    // Models the LC circuit's tendency to extend the boost upward
    // Gain = ~20% of peak gain, starting at 1.3x peak frequency
    double airDb = hbDb * 0.2;
    auto haShelf = juce::dsp::IIR::Coefficients<double>::makeHighShelf (
        sr, highF * 1.3, 0.5, juce::Decibels::decibelsToGain (airDb));
    *highAirL.coefficients = *haShelf; *highAirR.coefficients = *haShelf;

    // High atten — RC shelf cut at separate Atten Sel frequency
    auto haCoeff = juce::dsp::IIR::Coefficients<double>::makeHighShelf (
        sr, haFreq, 0.707, juce::Decibels::decibelsToGain (-haDb));
    *highAttenL.coefficients = *haCoeff; *highAttenR.coefficients = *haCoeff;

    // ════════════════════════════════════════════════════════
    //  MEQ-5 — Three mid-frequency bands with inductor modeling
    //
    //  The real MEQ-5 uses inductors for all 3 bands, creating
    //  asymmetric curves with slight overshoot/undershoot.
    //
    //  Low-mid peak: inductor creates steeper rolloff on high side
    //  → modeled as peak + slight dip above (asymmetry)
    //
    //  Mid dip: inductor creates wider dip with slight bump above
    //  → modeled as dip + small peak above (ringing)
    //
    //  High-mid peak: similar to low-mid, tighter Q
    //  → modeled as peak + slight dip below (asymmetry)
    //
    //  The overshoot is subtler than EQP-1A (different inductor values)
    // ════════════════════════════════════════════════════════

    // Low-Mid: peak boost + inductor skirt (dip above peak at ~1.5x freq)
    double lmDb = lowMidGain.load();
    double lmFreq = lowMidFreq.load();
    auto lm = juce::dsp::IIR::Coefficients<double>::makePeakFilter (
        sr, lmFreq, 1.3, juce::Decibels::decibelsToGain (lmDb));
    *lowMidL.coefficients = *lm; *lowMidR.coefficients = *lm;
    // Inductor asymmetry: slight dip above the peak (~25% of gain, Q=2.0)
    double lmSkirtDb = -lmDb * 0.25;
    auto lmSk = juce::dsp::IIR::Coefficients<double>::makePeakFilter (
        sr, juce::jmin (lmFreq * 1.5, sr * 0.45), 2.0, juce::Decibels::decibelsToGain (lmSkirtDb));
    *lowMidSkirtL.coefficients = *lmSk; *lowMidSkirtR.coefficients = *lmSk;

    // Mid Dip: peak cut + inductor ringing (slight bump above at ~1.6x freq)
    double mdDb = -midDipGain.load();
    double mdFreq = midDipFreq.load();
    auto md = juce::dsp::IIR::Coefficients<double>::makePeakFilter (
        sr, mdFreq, 0.9, juce::Decibels::decibelsToGain (mdDb));
    *midDipL.coefficients = *md; *midDipR.coefficients = *md;
    // Inductor ringing: slight bump above the dip (~20% of cut, Q=2.5)
    double mdSkirtDb = -mdDb * 0.2;  // positive, since mdDb is negative
    auto mdSk = juce::dsp::IIR::Coefficients<double>::makePeakFilter (
        sr, juce::jmin (mdFreq * 1.6, sr * 0.45), 2.5, juce::Decibels::decibelsToGain (mdSkirtDb));
    *midDipSkirtL.coefficients = *mdSk; *midDipSkirtR.coefficients = *mdSk;

    // High-Mid: peak boost + inductor skirt (dip below peak at ~0.7x freq)
    double hmDb = highMidGain.load();
    double hmFreq = highMidFreq.load();
    auto hm = juce::dsp::IIR::Coefficients<double>::makePeakFilter (
        sr, hmFreq, 1.4, juce::Decibels::decibelsToGain (hmDb));
    *highMidL.coefficients = *hm; *highMidR.coefficients = *hm;
    // Inductor asymmetry: slight dip below the peak (~20% of gain, Q=2.2)
    double hmSkirtDb = -hmDb * 0.2;
    auto hmSk = juce::dsp::IIR::Coefficients<double>::makePeakFilter (
        sr, hmFreq * 0.7, 2.2, juce::Decibels::decibelsToGain (hmSkirtDb));
    *highMidSkirtL.coefficients = *hmSk; *highMidSkirtR.coefficients = *hmSk;

    // ════════════════════════════════════════════════════════
    //  OUTPUT TRANSFORMER — Gentle HF rolloff
    //
    //  Real Pultec output transformer rolls off gently above 20kHz
    //  and adds a very subtle low-shelf warmth. We model this as
    //  a single low-pass shelf at 28kHz with -1.5 dB.
    // ════════════════════════════════════════════════════════

    double xfmrFreq = juce::jmin (sr * 0.45, 28000.0);  // respect Nyquist
    auto xf = juce::dsp::IIR::Coefficients<double>::makeHighShelf (
        sr, xfmrFreq, 0.5, juce::Decibels::decibelsToGain (-1.5));
    *xfmrL.coefficients = *xf; *xfmrR.coefficients = *xf;
}

void PultecEQStage::updateMidFilters()
{
    double sr = currentSampleRate; if (sr <= 0) return;
    // EQP-1A LOW
    double lowF = mLowBoostFreq.load();
    double lbDb = mLowBoostGain.load() * 1.2;
    double laDb = mLowAttenGain.load() * 1.2;
    auto lsC = juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr, lowF, 0.55, juce::Decibels::decibelsToGain(lbDb));
    *midLowShelf.coefficients = *lsC;
    auto lrC = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, lowF*1.4, 1.8, juce::Decibels::decibelsToGain(lbDb*0.35));
    *midLowResonance.coefficients = *lrC;
    auto laC = juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr, lowF, 1.4, juce::Decibels::decibelsToGain(-laDb));
    *midLowAtten.coefficients = *laC;
    // EQP-1A HIGH
    double highF = mHighBoostFreq.load();
    double hbDb = mHighBoostGain.load() * 1.2;
    double haDb = mHighAttenGain.load() * 1.2;
    double bwKnob = mHighAttenBW.load();
    double haFreq = mHighAttenFreq.load();
    double highQ = 4.5 * std::exp(-bwKnob * 0.26);
    auto hpC = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, highF, highQ, juce::Decibels::decibelsToGain(hbDb));
    *midHighPeak.coefficients = *hpC;
    auto haS = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, highF*1.3, 0.5, juce::Decibels::decibelsToGain(hbDb*0.2));
    *midHighAir.coefficients = *haS;
    auto haC = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, haFreq, 0.707, juce::Decibels::decibelsToGain(-haDb));
    *midHighAtten.coefficients = *haC;
    // MEQ-5
    double lmDb = mLowMidGain.load(), lmF = mLowMidFreq.load();
    auto lm = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, lmF, 1.3, juce::Decibels::decibelsToGain(lmDb));
    *midLowMid.coefficients = *lm;
    auto lmSk = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, juce::jmin(lmF*1.5, sr*0.45), 2.0, juce::Decibels::decibelsToGain(-lmDb*0.25));
    *midLowMidSkirt.coefficients = *lmSk;
    double mdDb = -mMidDipGain.load(), mdF = mMidDipFreq.load();
    auto md = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, mdF, 0.9, juce::Decibels::decibelsToGain(mdDb));
    *midMidDip.coefficients = *md;
    auto mdSk = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, juce::jmin(mdF*1.6, sr*0.45), 2.5, juce::Decibels::decibelsToGain(-mdDb*0.2));
    *midMidDipSkirt.coefficients = *mdSk;
    double hmDb = mHighMidGain.load(), hmF = mHighMidFreq.load();
    auto hm = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, hmF, 1.4, juce::Decibels::decibelsToGain(hmDb));
    *midHighMid.coefficients = *hm;
    auto hmSk = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, hmF*0.7, 2.2, juce::Decibels::decibelsToGain(-hmDb*0.2));
    *midHighMidSkirt.coefficients = *hmSk;
    // Transformer
    double xfF = juce::jmin(sr*0.45, 28000.0);
    auto xf = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, xfF, 0.5, juce::Decibels::decibelsToGain(-1.5));
    *midXfmr.coefficients = *xf;
}

void PultecEQStage::updateSideFilters()
{
    double sr = currentSampleRate; if (sr <= 0) return;
    // EQP-1A LOW
    double lowF = sLowBoostFreq.load();
    double lbDb = sLowBoostGain.load() * 1.2;
    double laDb = sLowAttenGain.load() * 1.2;
    auto lsC = juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr, lowF, 0.55, juce::Decibels::decibelsToGain(lbDb));
    *sideLowShelf.coefficients = *lsC;
    auto lrC = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, lowF*1.4, 1.8, juce::Decibels::decibelsToGain(lbDb*0.35));
    *sideLowResonance.coefficients = *lrC;
    auto laC = juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr, lowF, 1.4, juce::Decibels::decibelsToGain(-laDb));
    *sideLowAtten.coefficients = *laC;
    // EQP-1A HIGH
    double highF = sHighBoostFreq.load();
    double hbDb = sHighBoostGain.load() * 1.2;
    double haDb = sHighAttenGain.load() * 1.2;
    double bwKnob = sHighAttenBW.load();
    double haFreq = sHighAttenFreq.load();
    double highQ = 4.5 * std::exp(-bwKnob * 0.26);
    auto hpC = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, highF, highQ, juce::Decibels::decibelsToGain(hbDb));
    *sideHighPeak.coefficients = *hpC;
    auto haS = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, highF*1.3, 0.5, juce::Decibels::decibelsToGain(hbDb*0.2));
    *sideHighAir.coefficients = *haS;
    auto haC = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, haFreq, 0.707, juce::Decibels::decibelsToGain(-haDb));
    *sideHighAtten.coefficients = *haC;
    // MEQ-5
    double lmDb = sLowMidGain.load(), lmF = sLowMidFreq.load();
    auto lm = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, lmF, 1.3, juce::Decibels::decibelsToGain(lmDb));
    *sideLowMid.coefficients = *lm;
    auto lmSk = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, juce::jmin(lmF*1.5, sr*0.45), 2.0, juce::Decibels::decibelsToGain(-lmDb*0.25));
    *sideLowMidSkirt.coefficients = *lmSk;
    double mdDb = -sMidDipGain.load(), mdF = sMidDipFreq.load();
    auto md = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, mdF, 0.9, juce::Decibels::decibelsToGain(mdDb));
    *sideMidDip.coefficients = *md;
    auto mdSk = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, juce::jmin(mdF*1.6, sr*0.45), 2.5, juce::Decibels::decibelsToGain(-mdDb*0.2));
    *sideMidDipSkirt.coefficients = *mdSk;
    double hmDb = sHighMidGain.load(), hmF = sHighMidFreq.load();
    auto hm = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, hmF, 1.4, juce::Decibels::decibelsToGain(hmDb));
    *sideHighMid.coefficients = *hm;
    auto hmSk = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, hmF*0.7, 2.2, juce::Decibels::decibelsToGain(-hmDb*0.2));
    *sideHighMidSkirt.coefficients = *hmSk;
    // Transformer
    double xfF = juce::jmin(sr*0.45, 28000.0);
    auto xf = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, xfF, 0.5, juce::Decibels::decibelsToGain(-1.5));
    *sideXfmr.coefficients = *xf;
}

void PultecEQStage::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S2_EQ_On","Pultec EQ On",true));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_MS","Channel",juce::StringArray{"Stereo","Mid","Side"},0));
    // ─── Stereo params ───
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_LowBoost_Freq","Low Freq",juce::StringArray{"20","30","60","100"},2));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowBoost_Gain","Low Boost",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowAtten_Gain","Low Atten",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_HighBoost_Freq","High Freq",juce::StringArray{"3k","4k","5k","8k","10k","12k","16k"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighBoost_Gain","High Boost",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighAtten_Gain","High Atten",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_HighAtten_Freq","Atten Sel",juce::StringArray{"5k","10k","20k"},1));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighAtten_BW","Bandwidth",juce::NormalisableRange<float>(0,10,0.1f),5));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowMid_Freq","LM Freq",juce::NormalisableRange<float>(200,1000,1,0.4f),200));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowMid_Gain","LM Peak",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_MidDip_Freq","Dip Freq",juce::NormalisableRange<float>(200,7000,1,0.35f),1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_MidDip_Gain","Dip",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighMid_Freq","HM Freq",juce::NormalisableRange<float>(1500,5000,1,0.3f),1500));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighMid_Gain","HM Peak",juce::NormalisableRange<float>(0,10,0.1f),0));
    // ─── Mid params (M/S mode) ───
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_M_LowBoost_Freq","M Low Freq",juce::StringArray{"20","30","60","100"},2));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_M_LowBoost_Gain","M Low Boost",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_M_LowAtten_Gain","M Low Atten",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_M_HighBoost_Freq","M High Freq",juce::StringArray{"3k","4k","5k","8k","10k","12k","16k"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_M_HighBoost_Gain","M High Boost",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_M_HighAtten_Gain","M High Atten",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_M_HighAtten_Freq","M Atten Sel",juce::StringArray{"5k","10k","20k"},1));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_M_HighAtten_BW","M Bandwidth",juce::NormalisableRange<float>(0,10,0.1f),5));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_M_LowMid_Freq","M LM Freq",juce::NormalisableRange<float>(200,1000,1,0.4f),200));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_M_LowMid_Gain","M LM Peak",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_M_MidDip_Freq","M Dip Freq",juce::NormalisableRange<float>(200,7000,1,0.35f),1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_M_MidDip_Gain","M Dip",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_M_HighMid_Freq","M HM Freq",juce::NormalisableRange<float>(1500,5000,1,0.3f),1500));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_M_HighMid_Gain","M HM Peak",juce::NormalisableRange<float>(0,10,0.1f),0));
    // ─── Side params (M/S mode) ───
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_S_LowBoost_Freq","S Low Freq",juce::StringArray{"20","30","60","100"},2));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_S_LowBoost_Gain","S Low Boost",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_S_LowAtten_Gain","S Low Atten",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_S_HighBoost_Freq","S High Freq",juce::StringArray{"3k","4k","5k","8k","10k","12k","16k"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_S_HighBoost_Gain","S High Boost",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_S_HighAtten_Gain","S High Atten",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_S_HighAtten_Freq","S Atten Sel",juce::StringArray{"5k","10k","20k"},1));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_S_HighAtten_BW","S Bandwidth",juce::NormalisableRange<float>(0,10,0.1f),5));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_S_LowMid_Freq","S LM Freq",juce::NormalisableRange<float>(200,1000,1,0.4f),200));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_S_LowMid_Gain","S LM Peak",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_S_MidDip_Freq","S Dip Freq",juce::NormalisableRange<float>(200,7000,1,0.35f),1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_S_MidDip_Gain","S Dip",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_S_HighMid_Freq","S HM Freq",juce::NormalisableRange<float>(1500,5000,1,0.3f),1500));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_S_HighMid_Gain","S HM Peak",juce::NormalisableRange<float>(0,10,0.1f),0));
}

void PultecEQStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store (a.getRawParameterValue("S2_EQ_On")->load() > 0.5f);
    msMode.store ((int) a.getRawParameterValue("S2_EQ_MS")->load());
    static const float lowFreqs[] = { 20.0f, 30.0f, 60.0f, 100.0f };
    static const float highFreqs[] = { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
    static const float hAttenFreqs[] = { 5000.0f, 10000.0f, 20000.0f };

    // Stereo params
    int lowIdx = juce::jlimit (0, 3, (int) a.getRawParameterValue("S2_EQ_LowBoost_Freq")->load());
    lowBoostFreq.store (lowFreqs[lowIdx]);
    lowBoostGain.store (a.getRawParameterValue("S2_EQ_LowBoost_Gain")->load());
    lowAttenGain.store (a.getRawParameterValue("S2_EQ_LowAtten_Gain")->load());
    int highIdx = juce::jlimit (0, 6, (int) a.getRawParameterValue("S2_EQ_HighBoost_Freq")->load());
    highBoostFreq.store (highFreqs[highIdx]);
    highBoostGain.store (a.getRawParameterValue("S2_EQ_HighBoost_Gain")->load());
    highAttenGain.store (a.getRawParameterValue("S2_EQ_HighAtten_Gain")->load());
    int hAttenIdx = juce::jlimit (0, 2, (int) a.getRawParameterValue("S2_EQ_HighAtten_Freq")->load());
    highAttenFreq.store (hAttenFreqs[hAttenIdx]);
    highAttenBW.store (a.getRawParameterValue("S2_EQ_HighAtten_BW")->load());
    lowMidFreq.store (a.getRawParameterValue("S2_EQ_LowMid_Freq")->load());
    lowMidGain.store (a.getRawParameterValue("S2_EQ_LowMid_Gain")->load());
    midDipFreq.store (a.getRawParameterValue("S2_EQ_MidDip_Freq")->load());
    midDipGain.store (a.getRawParameterValue("S2_EQ_MidDip_Gain")->load());
    highMidFreq.store (a.getRawParameterValue("S2_EQ_HighMid_Freq")->load());
    highMidGain.store (a.getRawParameterValue("S2_EQ_HighMid_Gain")->load());
    updateFilters();

    // Mid params
    int mLowIdx = juce::jlimit (0, 3, (int) a.getRawParameterValue("S2_EQ_M_LowBoost_Freq")->load());
    mLowBoostFreq.store (lowFreqs[mLowIdx]);
    mLowBoostGain.store (a.getRawParameterValue("S2_EQ_M_LowBoost_Gain")->load());
    mLowAttenGain.store (a.getRawParameterValue("S2_EQ_M_LowAtten_Gain")->load());
    int mHighIdx = juce::jlimit (0, 6, (int) a.getRawParameterValue("S2_EQ_M_HighBoost_Freq")->load());
    mHighBoostFreq.store (highFreqs[mHighIdx]);
    mHighBoostGain.store (a.getRawParameterValue("S2_EQ_M_HighBoost_Gain")->load());
    mHighAttenGain.store (a.getRawParameterValue("S2_EQ_M_HighAtten_Gain")->load());
    int mHAttenIdx = juce::jlimit (0, 2, (int) a.getRawParameterValue("S2_EQ_M_HighAtten_Freq")->load());
    mHighAttenFreq.store (hAttenFreqs[mHAttenIdx]);
    mHighAttenBW.store (a.getRawParameterValue("S2_EQ_M_HighAtten_BW")->load());
    mLowMidFreq.store (a.getRawParameterValue("S2_EQ_M_LowMid_Freq")->load());
    mLowMidGain.store (a.getRawParameterValue("S2_EQ_M_LowMid_Gain")->load());
    mMidDipFreq.store (a.getRawParameterValue("S2_EQ_M_MidDip_Freq")->load());
    mMidDipGain.store (a.getRawParameterValue("S2_EQ_M_MidDip_Gain")->load());
    mHighMidFreq.store (a.getRawParameterValue("S2_EQ_M_HighMid_Freq")->load());
    mHighMidGain.store (a.getRawParameterValue("S2_EQ_M_HighMid_Gain")->load());
    updateMidFilters();

    // Side params
    int sLowIdx = juce::jlimit (0, 3, (int) a.getRawParameterValue("S2_EQ_S_LowBoost_Freq")->load());
    sLowBoostFreq.store (lowFreqs[sLowIdx]);
    sLowBoostGain.store (a.getRawParameterValue("S2_EQ_S_LowBoost_Gain")->load());
    sLowAttenGain.store (a.getRawParameterValue("S2_EQ_S_LowAtten_Gain")->load());
    int sHighIdx = juce::jlimit (0, 6, (int) a.getRawParameterValue("S2_EQ_S_HighBoost_Freq")->load());
    sHighBoostFreq.store (highFreqs[sHighIdx]);
    sHighBoostGain.store (a.getRawParameterValue("S2_EQ_S_HighBoost_Gain")->load());
    sHighAttenGain.store (a.getRawParameterValue("S2_EQ_S_HighAtten_Gain")->load());
    int sHAttenIdx = juce::jlimit (0, 2, (int) a.getRawParameterValue("S2_EQ_S_HighAtten_Freq")->load());
    sHighAttenFreq.store (hAttenFreqs[sHAttenIdx]);
    sHighAttenBW.store (a.getRawParameterValue("S2_EQ_S_HighAtten_BW")->load());
    sLowMidFreq.store (a.getRawParameterValue("S2_EQ_S_LowMid_Freq")->load());
    sLowMidGain.store (a.getRawParameterValue("S2_EQ_S_LowMid_Gain")->load());
    sMidDipFreq.store (a.getRawParameterValue("S2_EQ_S_MidDip_Freq")->load());
    sMidDipGain.store (a.getRawParameterValue("S2_EQ_S_MidDip_Gain")->load());
    sHighMidFreq.store (a.getRawParameterValue("S2_EQ_S_HighMid_Freq")->load());
    sHighMidGain.store (a.getRawParameterValue("S2_EQ_S_HighMid_Gain")->load());
    updateSideFilters();
}

// ─────────────────────────────────────────────────────────────
//  COMPRESSOR STAGE
// ─────────────────────────────────────────────────────────────

void CompressorStage::prepare (double sr, int bs)
{
    currentSampleRate = sr; currentBlockSize = bs;
    envelope = 0; optoGR = 0; optoFastGR = 0; optoCellHistory = 0;
    fetGR = 0; fetFeedbackEnv = 0; variMuBias = 0; variMuEnv = 0;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 1 };
    scHpL.prepare (spec); scHpR.prepare (spec);
    updateCoefficients();
}

void CompressorStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters (block);
    int n = (int) block.getNumSamples();
    auto* l = block.getChannelPointer (0);
    auto* r = block.getChannelPointer (1);
    double dryMix = 1.0 - (mix.load() / 100.0), wetMix = mix.load() / 100.0;
    double makeup = juce::Decibels::decibelsToGain ((double) makeupGain.load());
    int mdl = model.load();

    for (int i = 0; i < n; ++i)
    {
        double dL = l[i], dR = r[i];

        // Sidechain with HP filter
        double sL = scHpL.processSample (l[i]);
        double sR = scHpR.processSample (r[i]);

        // Detection: peak for VCA/FET, RMS-ish for Opto/Vari-Mu
        double peak = std::max (std::abs (sL), std::abs (sR));
        double peakDb = juce::Decibels::gainToDecibels (peak, -100.0);

        // RMS approximation (smoothed squared level) for Opto/Vari-Mu
        double rmsLin = (sL * sL + sR * sR) * 0.5;
        double rmsDb = juce::Decibels::gainToDecibels (std::sqrt (rmsLin), -100.0);

        // Model-specific gain reduction
        double grDb = 0.0;
        switch (mdl)
        {
            case 0:  grDb = processVCA (peakDb);    break;
            case 1:  grDb = processOpto (rmsDb);    break;
            case 2:  grDb = processFET (peakDb);    break;
            case 3:  grDb = processVariMu (rmsDb);  break;
            default: grDb = processVCA (peakDb);    break;
        }

        double grLin = juce::Decibels::decibelsToGain (grDb);

        // Apply GR + model-specific saturation
        double wetL = l[i] * grLin;
        double wetR = r[i] * grLin;

        if (mdl == 2) // FET adds harmonic distortion proportional to GR
        {
            double grAmount = std::abs (grDb);
            wetL = fetSaturate (wetL, grAmount);
            wetR = fetSaturate (wetR, grAmount);
        }
        else if (mdl == 3) // Vari-Mu adds subtle tube warmth
        {
            wetL = variMuSaturate (wetL);
            wetR = variMuSaturate (wetR);
        }

        l[i] = dL * dryMix + wetL * makeup * wetMix;
        r[i] = dR * dryMix + wetR * makeup * wetMix;
        meterData.gainReduction.store ((float) grDb);

        // Write to history every ~256 samples (slower scroll for readability)
        if ((i & 255) == 0)
        {
            int wp = grHistoryPos.load (std::memory_order_relaxed);
            grHistory[(size_t) wp] = (float) grDb;
            inputHistory[(size_t) wp] = (float) juce::Decibels::gainToDecibels (peak, -60.0);
            grHistoryPos.store ((wp + 1) % GR_HISTORY_SIZE, std::memory_order_relaxed);
        }
    }
    updateOutputMeters (block);
}

// ════════════════════════════════════════════════════════
//  VCA — SSL G-Bus style
//
//  Feed-forward topology. Hard knee. Precise, punchy.
//  The VCA (THAT 2181) responds instantly to the control
//  voltage. No inherent coloration — the sound is all
//  about the envelope shape and the sidechain.
// ════════════════════════════════════════════════════════

double CompressorStage::processVCA (double peakDb)
{
    double sr = currentSampleRate;
    double t = threshold.load();
    double r = ratio.load();

    // Feed-forward detection: level comparison is instantaneous
    double over = peakDb - t;
    if (over < 0) over = 0;

    // Hard knee gain reduction
    double targetGR = -(over * (1.0 - 1.0 / r));

    // Smooth envelope with attack/release
    if (targetGR < envelope)
        envelope = attackCoeff * envelope + (1.0 - attackCoeff) * targetGR;
    else
    {
        double rc = releaseCoeff;
        if (autoRelease.load())
        {
            // SSL-style auto-release: faster for transients, slower for sustained
            double grAmt = std::abs (envelope);
            double ms = juce::jlimit (30.0, 600.0, juce::jmap (grAmt, 0.0, 12.0, 50.0, 400.0));
            rc = std::exp (-1.0 / (sr * ms / 1000.0));
        }
        envelope = rc * envelope + (1.0 - rc) * targetGR;
    }
    return envelope;
}

// ════════════════════════════════════════════════════════
//  OPTO — LA-2A style
//
//  The T4B opto cell has program-dependent behavior:
//  - Attack slows down as gain reduction increases
//  - Release has two phases: fast (~60ms) then slow (1-3s)
//  - The cell has "memory" — repeated compression makes
//    release even slower (electroluminescent persistence)
//  - Detection is RMS-based (the light integrates power)
//  - Very smooth, musical compression
// ════════════════════════════════════════════════════════

double CompressorStage::processOpto (double rmsDb)
{
    double sr = currentSampleRate;
    double t = threshold.load();
    double r = ratio.load();

    double over = rmsDb - t;
    if (over < 0) over = 0;

    // Soft knee (natural from opto cell response) — 6 dB knee
    double knee = 6.0;
    double grTarget = 0.0;
    if (over < knee)
        grTarget = -(over * over / (2.0 * knee)) * (1.0 - 1.0 / r);
    else
        grTarget = -(over * (1.0 - 1.0 / r));

    // T4B cell: attack slows with GR amount
    // Light onset is fast (10-20ms), but full brightness takes longer
    double atMs = attackMs.load();
    double currentGR = std::abs (optoGR);
    double modifiedAttack = atMs * (1.0 + currentGR * 0.15); // attack slows 15% per dB GR
    double ac = std::exp (-1.0 / (sr * modifiedAttack / 1000.0));

    // Two-phase release: fast phase (~60ms) + slow phase (scales with history)
    double fastRelMs = 60.0;
    double slowRelMs = releaseMs.load() * (1.0 + optoCellHistory * 0.3); // cell memory
    double fastRC = std::exp (-1.0 / (sr * fastRelMs / 1000.0));
    double slowRC = std::exp (-1.0 / (sr * slowRelMs / 1000.0));

    // Fast GR responds to attack
    if (grTarget < optoFastGR)
        optoFastGR = ac * optoFastGR + (1.0 - ac) * grTarget;
    else
        optoFastGR = fastRC * optoFastGR + (1.0 - fastRC) * grTarget;

    // Main opto cell: follows fast GR but with slow release
    if (optoFastGR < optoGR)
        optoGR = optoFastGR; // attack: instant follow
    else
        optoGR = slowRC * optoGR + (1.0 - slowRC) * 0.0; // slow release toward 0

    // Cell memory: accumulates with compression, decays slowly
    if (grTarget < -0.5)
        optoCellHistory = std::min (10.0, optoCellHistory + 0.001);
    else
        optoCellHistory *= 0.9999; // very slow decay

    return optoGR;
}

// ════════════════════════════════════════════════════════
//  FET — 1176 style
//
//  Feedback topology: the FET (Field Effect Transistor)
//  acts as a variable resistor in a feedback loop.
//  Characteristics:
//  - Ultra-fast attack (20μs to 800μs)
//  - Program-dependent release (faster at higher GR)
//  - FET adds harmonic distortion that increases with GR
//  - Feedback topology = the detector sees the COMPRESSED
//    signal, creating a more "aggressive" character
//  - The 1176's "all-buttons" mode: all ratios engaged
//    simultaneously → creates a unique clipping behavior
// ════════════════════════════════════════════════════════

double CompressorStage::processFET (double peakDb)
{
    double sr = currentSampleRate;
    double t = threshold.load();
    double r = ratio.load();

    // ─── 1176 Feedback topology ───
    // The detector sees the OUTPUT level, not the input.
    // This creates the characteristic "grabbing" behavior:
    // as GR increases, the detected level drops, so the compressor
    // partially releases, creating a natural pump/breathe.
    double detectedLevel = peakDb + fetGR * 0.7; // 70% feedback (not 100% = more stable)
    double over = detectedLevel - t;
    if (over < 0) over = 0;

    // ─── 1176 ratio behavior ───
    // Real 1176 has 4 positions: 4:1, 8:1, 12:1, 20:1
    // At 20:1 it's nearly limiting. "All-buttons" (~100:1) creates
    // a unique distortion where the FET is driven into saturation.
    // The knee gets harder at higher ratios (FET characteristic)
    double knee = juce::jmap (r, 1.0, 20.0, 8.0, 1.0); // softer at low ratio, hard at high
    double grTarget;
    if (over < knee)
        grTarget = -(over * over / (2.0 * knee)) * (1.0 - 1.0 / r);
    else
        grTarget = -(over * (1.0 - 1.0 / r));

    // ─── 1176 attack: 20μs to 800μs (7 positions) ───
    // The attack knob is reversed on the 1176: fully CW = fastest
    double atMs = juce::jlimit (0.02, 0.8, (double) attackMs.load());
    double ac = std::exp (-1.0 / (sr * atMs / 1000.0));

    // ─── 1176 release: program-dependent with two-stage decay ───
    // Fast initial release, then slower tail (FET capacitor discharge curve)
    double relMs = releaseMs.load();
    double grAmount = std::abs (fetGR);

    // Two-stage release: fast (50ms base) blended with user release
    double fastRelMs = 50.0;
    double slowRelMs = relMs;
    // At deep GR, the fast phase dominates (capacitor discharges faster under load)
    double fastWeight = juce::jlimit (0.0, 0.8, grAmount * 0.06);
    double effectiveRelMs = fastRelMs * fastWeight + slowRelMs * (1.0 - fastWeight);
    effectiveRelMs = juce::jlimit (20.0, 1200.0, effectiveRelMs);
    double rc = std::exp (-1.0 / (sr * effectiveRelMs / 1000.0));

    // ─── Envelope with 1176-style "snap" ───
    // The attack has a slight overshoot (capacitor charge through FET)
    if (grTarget < fetGR)
    {
        fetGR = ac * fetGR + (1.0 - ac) * grTarget;
        // Overshoot: momentarily compress MORE than target (0.5-1 dB)
        double overshoot = (fetGR - grTarget) * 0.15;
        fetGR -= overshoot;
    }
    else
    {
        fetGR = rc * fetGR + (1.0 - rc) * grTarget;
    }

    return fetGR;
}

double CompressorStage::fetSaturate (double x, double grAmount) const
{
    // ─── 1176 FET + output transformer saturation ───
    // The 2N3631 FET adds odd harmonics (3rd dominant) when
    // driven into pinch-off. The output transformer adds even harmonics.
    // Combined: rich, aggressive harmonic content.

    // Drive amount scales with GR (more compression = more color)
    double drive = 1.0 + grAmount * 0.05; // 5% per dB
    double xd = x * drive;

    // FET transfer curve: asymmetric with sharper negative clip
    // (drain current cuts off harder on negative swing)
    double y;
    if (xd >= 0.0)
        y = std::tanh (xd * 0.8) / 0.8; // soft positive (transformer)
    else
        y = xd / (1.0 + 0.4 * xd * xd); // harder negative (FET pinch-off)

    // Output transformer: adds subtle 2nd harmonic
    y += 0.02 * xd * std::abs (xd); // 2nd harmonic generator

    // Blend: proportional to compression depth
    double wetAmount = juce::jlimit (0.02, 0.2, grAmount * 0.015);
    return x * (1.0 - wetAmount) + y * wetAmount;
}

// ════════════════════════════════════════════════════════
//  VARI-MU — Fairchild 670 style
//
//  The tube itself IS the gain reduction element.
//  The bias point of the tube shifts with the sidechain,
//  changing the tube's gain. This creates:
//  - Extremely soft knee (it's inherent in the tube curve)
//  - Very musical, "glue" compression
//  - Natural tube harmonics (even-order dominant)
//  - Slow, program-dependent behavior
//  - The Fairchild has 6 fixed time constants, but we
//    allow continuous control for flexibility
// ════════════════════════════════════════════════════════

double CompressorStage::processVariMu (double rmsDb)
{
    double sr = currentSampleRate;
    double t = threshold.load();
    double r = ratio.load();

    double over = rmsDb - t;
    if (over < 0) over = 0;

    // Very soft knee — tube bias shift is gradual
    // Models the tube's transconductance curve
    double targetGR = 0.0;
    if (over > 0)
    {
        // Soft saturation curve (tanh-like) — the tube naturally compresses
        double normalizedOver = over / 20.0; // normalize to ~1.0 range
        double tanhOver = std::tanh (normalizedOver * r * 0.25) * 20.0;
        targetGR = -(tanhOver * (1.0 - 1.0 / r));
    }

    // Tube bias responds slowly — large capacitors in the sidechain
    double atMs = juce::jmax (5.0, (double) attackMs.load()); // minimum 5ms (tubes can't respond faster)
    double ac = std::exp (-1.0 / (sr * atMs / 1000.0));

    double relMs = juce::jmax (100.0, (double) releaseMs.load()); // minimum 100ms
    double rc = std::exp (-1.0 / (sr * relMs / 1000.0));

    // Tube bias envelope — very smooth
    if (targetGR < variMuBias)
        variMuBias = ac * variMuBias + (1.0 - ac) * targetGR;
    else
        variMuBias = rc * variMuBias + (1.0 - rc) * targetGR;

    // Second smoothing stage — models the tube's thermal inertia
    variMuEnv = 0.995 * variMuEnv + 0.005 * variMuBias;

    // Return the double-smoothed envelope (super smooth "glue")
    return variMuEnv;
}

double CompressorStage::variMuSaturate (double x) const
{
    // Tube output transformer: subtle even harmonics
    // 2nd harmonic dominant (~2% THD), warm character
    double xd = x * 1.05;
    double y;
    if (xd >= 0.0)
        y = 1.0 - std::exp (-xd);
    else
        y = -(1.0 - std::exp (xd * 0.9)) * 0.95;

    return x * 0.95 + y * 0.05; // 5% tube blend
}

void CompressorStage::reset()
{
    envelope = 0; scHpL.reset(); scHpR.reset();
    optoGR = 0; optoFastGR = 0; optoCellHistory = 0;
    fetGR = 0; fetFeedbackEnv = 0;
    variMuBias = 0; variMuEnv = 0;
}

void CompressorStage::updateCoefficients()
{
    double sr = currentSampleRate; if (sr <= 0) return;
    attackCoeff = std::exp (-1.0 / (sr * attackMs.load() / 1000.0));
    releaseCoeff = std::exp (-1.0 / (sr * releaseMs.load() / 1000.0));
    auto c = juce::dsp::IIR::Coefficients<double>::makeHighPass (sr, scHpFreq.load());
    *scHpL.coefficients = *c; *scHpR.coefficients = *c;
}

void CompressorStage::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S3_Comp_On","Comp On",true));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S3_Comp_Model","Model",juce::StringArray{"VCA (SSL)","Opto (LA-2A)","FET (1176)","Vari-Mu (Fairchild)"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Threshold","Threshold",juce::NormalisableRange<float>(-60,0,0.1f),-20));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Ratio","Ratio",juce::NormalisableRange<float>(1,20,0.1f,0.5f),4));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Attack","Attack",juce::NormalisableRange<float>(0.02f,100,0.01f,0.3f),10));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Release","Release",juce::NormalisableRange<float>(10,2000,1,0.4f),100));
    layout.add(std::make_unique<juce::AudioParameterBool>("S3_Comp_AutoRelease","Auto Release",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Makeup","Makeup",juce::NormalisableRange<float>(0,24,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_Mix","Mix",juce::NormalisableRange<float>(0,100,1),100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S3_Comp_SC_HP","SC HP",juce::NormalisableRange<float>(20,500,1,0.4f),20));
}

void CompressorStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store (a.getRawParameterValue("S3_Comp_On")->load() > 0.5f);
    model.store ((int) a.getRawParameterValue("S3_Comp_Model")->load());
    threshold.store (a.getRawParameterValue("S3_Comp_Threshold")->load());
    ratio.store (a.getRawParameterValue("S3_Comp_Ratio")->load());
    attackMs.store (a.getRawParameterValue("S3_Comp_Attack")->load());
    releaseMs.store (a.getRawParameterValue("S3_Comp_Release")->load());
    autoRelease.store (a.getRawParameterValue("S3_Comp_AutoRelease")->load() > 0.5f);
    makeupGain.store (a.getRawParameterValue("S3_Comp_Makeup")->load());
    mix.store (a.getRawParameterValue("S3_Comp_Mix")->load());
    scHpFreq.store (a.getRawParameterValue("S3_Comp_SC_HP")->load());
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
    xover1LP.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    xover1HP.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    xover2LP.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    xover2HP.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    xover3LP.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    xover3HP.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    for (auto& b:bandBuffers) b.setSize(2,bs);
    tempBuffer.setSize(2,bs);
    // Linear phase crossover (3 parallel LP FIR filters)
    linXoverLP1.prepare(sr, bs);
    linXoverLP2.prepare(sr, bs);
    linXoverLP3.prepare(sr, bs);
    linXoverBuilt = false; lastXF1 = -1; lastXF2 = -1; lastXF3 = -1;
    lp1Buf.setSize(2, bs); lp2Buf.setSize(2, bs); lp3Buf.setSize(2, bs);
    inputDelayBuf.setSize(2, LP_DELAY + bs + 1); inputDelayBuf.clear(); inputDelayWP = 0;
}

void SaturationStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters(block);
    int n=(int)block.getNumSamples();

    // M/S mode: encode to M/S domain
    int ms = msMode.load();
    if (ms > 0)
    {
        auto* ch0 = block.getChannelPointer (0);
        auto* ch1 = block.getChannelPointer (1);
        for (int i = 0; i < n; ++i)
        {
            double mid  = (ch0[i] + ch1[i]) * 0.5;
            double side = (ch0[i] - ch1[i]) * 0.5;
            ch0[i] = mid; ch1[i] = side;
        }
    }

    if (mode.load()==0)
    {
        // Single-band mode
        if (ms > 0)
        {
            // ─── M/S DUAL: independent Mid + Side saturation ───
            // Mid channel (ch0) with Mid params
            double mDrv=juce::Decibels::decibelsToGain((double)mDrive.load());
            double mOut=juce::Decibels::decibelsToGain((double)mOutput.load());
            double mBld=mBlend.load()/100.0;
            int mSt=mSatType.load();
            double mB=mBits.load(), mR=mRate.load();
            double mSrRatio = (currentSampleRate > 0) ? mR / currentSampleRate : 1.0;
            {
                auto* d=block.getChannelPointer(0);
                for (int i=0;i<n;++i)
                {
                    double dry=d[i], input_s=dry;
                    if (mSt==4 && mSrRatio<1.0)
                    {
                        midSRCounter+=mSrRatio;
                        if (midSRCounter>=1.0){midSRCounter-=1.0;midSRHold=input_s;}
                        input_s=midSRHold;
                    }
                    d[i]=dry*(1-mBld)+saturateSample(input_s,mSt,mDrv,mB,mR)*mOut*mBld;
                }
            }
            // Side channel (ch1) with Side params
            double sDrv=juce::Decibels::decibelsToGain((double)sDrive.load());
            double sOut=juce::Decibels::decibelsToGain((double)sOutput.load());
            double sBld=sBlend.load()/100.0;
            int sSt=sSatType.load();
            double sB=sBits.load(), sR=sRate.load();
            double sSrRatio = (currentSampleRate > 0) ? sR / currentSampleRate : 1.0;
            {
                auto* d=block.getChannelPointer(1);
                for (int i=0;i<n;++i)
                {
                    double dry=d[i], input_s=dry;
                    if (sSt==4 && sSrRatio<1.0)
                    {
                        sideSRCounter+=sSrRatio;
                        if (sideSRCounter>=1.0){sideSRCounter-=1.0;sideSRHold=input_s;}
                        input_s=sideSRHold;
                    }
                    d[i]=dry*(1-sBld)+saturateSample(input_s,sSt,sDrv,sB,sR)*sOut*sBld;
                }
            }
        }
        else
        {
            // ─── STEREO: same params both channels ───
            double drv=juce::Decibels::decibelsToGain((double)drive.load());
            double out=juce::Decibels::decibelsToGain((double)output.load());
            double bld=blend.load()/100.0;
            int st=satType.load();
            double b=bits.load();
            double r=rate.load();
            double srRatio = (currentSampleRate > 0) ? r / currentSampleRate : 1.0;

            for (int ch=0;ch<2;++ch)
            {
                auto* d=block.getChannelPointer(ch);
                for (int i=0;i<n;++i)
                {
                    double dry=d[i];
                    double input_s = dry;
                    if (st == 4 && srRatio < 1.0)
                    {
                        globalSRCounter += srRatio;
                        if (globalSRCounter >= 1.0) { globalSRCounter -= 1.0; globalSRHold = input_s; }
                        input_s = globalSRHold;
                    }
                    d[i] = dry*(1-bld) + saturateSample(input_s, st, drv, b, r)*out*bld;
                }
            }
        }
    }
    else
    {
        // ─── MULTIBAND MODE — proper 4-way crossover split ──
        for (auto& buf:bandBuffers) buf.setSize(2,n,false,false,true);
        tempBuffer.setSize(2,n,false,false,true);

        bool useLinearPhase = (xoverMode.load() == 1) && linXoverBuilt;

        if (useLinearPhase)
        {
            // ─── Linear Phase crossover: PARALLEL LP approach ───
            //
            // Apply 3 LP filters to the SAME input in parallel.
            // All 3 have identical latency = LP_DELAY (256 samples).
            //
            // band0 = LP1(input)                    → low below f1
            // band1 = LP2(input) - LP1(input)       → bandpass f1-f2
            // band2 = LP3(input) - LP2(input)       → bandpass f2-f3
            // band3 = input_delayed - LP3(input)    → high above f3
            //
            // Sum = LP1 + (LP2-LP1) + (LP3-LP2) + (del-LP3) = del = perfect!

            lp1Buf.setSize (2, n, false, false, true);
            lp2Buf.setSize (2, n, false, false, true);
            lp3Buf.setSize (2, n, false, false, true);

            // Copy input into 3 LP buffers
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* src = block.getChannelPointer (ch);
                juce::FloatVectorOperations::copy (lp1Buf.getWritePointer (ch), src, n);
                juce::FloatVectorOperations::copy (lp2Buf.getWritePointer (ch), src, n);
                juce::FloatVectorOperations::copy (lp3Buf.getWritePointer (ch), src, n);
            }

            // Apply 3 LP filters in parallel (all see the same input)
            { juce::dsp::AudioBlock<double> b (lp1Buf); linXoverLP1.process (b); }
            { juce::dsp::AudioBlock<double> b (lp2Buf); linXoverLP2.process (b); }
            { juce::dsp::AudioBlock<double> b (lp3Buf); linXoverLP3.process (b); }

            // Delay raw input by LP_DELAY to match FIR latency
            int dlBufSize = inputDelayBuf.getNumSamples();
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* src = block.getChannelPointer (ch);
                auto* dly = inputDelayBuf.getWritePointer (ch);
                auto* delayed = bandBuffers[3].getWritePointer (ch); // temp use

                for (int i = 0; i < n; ++i)
                {
                    int wIdx = (inputDelayWP + i) % dlBufSize;
                    int rIdx = (wIdx - LP_DELAY + dlBufSize) % dlBufSize;
                    delayed[i] = dly[rIdx];
                    dly[wIdx] = src[i];
                }
            }
            inputDelayWP = (inputDelayWP + n) % dlBufSize;

            // Compute 4 bands by subtraction
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* l1 = lp1Buf.getReadPointer (ch);
                auto* l2 = lp2Buf.getReadPointer (ch);
                auto* l3 = lp3Buf.getReadPointer (ch);
                auto* del = bandBuffers[3].getReadPointer (ch); // delayed input

                auto* b0 = bandBuffers[0].getWritePointer (ch);
                auto* b1 = bandBuffers[1].getWritePointer (ch);
                auto* b2 = bandBuffers[2].getWritePointer (ch);
                auto* b3 = bandBuffers[3].getWritePointer (ch);

                for (int i = 0; i < n; ++i)
                {
                    b0[i] = l1[i];               // low
                    b1[i] = l2[i] - l1[i];       // lo-mid
                    b2[i] = l3[i] - l2[i];       // hi-mid
                    b3[i] = del[i] - l3[i];      // high
                }
            }
        }
        else
        {
            // ─── Minimum Phase LR crossover ───
            // Update crossover frequencies
            xover1LP.setCutoffFrequency(xoverFreq1.load());
            xover1HP.setCutoffFrequency(xoverFreq1.load());
            xover2LP.setCutoffFrequency(xoverFreq2.load());
            xover2HP.setCutoffFrequency(xoverFreq2.load());
            xover3LP.setCutoffFrequency(xoverFreq3.load());
            xover3HP.setCutoffFrequency(xoverFreq3.load());

            // Split at xover1
            for (int ch=0;ch<2;++ch)
            {
                auto* src=block.getChannelPointer(ch);
                juce::FloatVectorOperations::copy(bandBuffers[0].getWritePointer(ch),src,n);
                juce::FloatVectorOperations::copy(tempBuffer.getWritePointer(ch),src,n);
            }
            { juce::dsp::AudioBlock<double> b(bandBuffers[0]); juce::dsp::ProcessContextReplacing<double> c(b); xover1LP.process(c); }
            { juce::dsp::AudioBlock<double> b(tempBuffer); juce::dsp::ProcessContextReplacing<double> c(b); xover1HP.process(c); }

            // Split at xover2
            for (int ch=0;ch<2;++ch)
            {
                juce::FloatVectorOperations::copy(bandBuffers[1].getWritePointer(ch),tempBuffer.getReadPointer(ch),n);
                juce::FloatVectorOperations::copy(bandBuffers[2].getWritePointer(ch),tempBuffer.getReadPointer(ch),n);
            }
            { juce::dsp::AudioBlock<double> b(bandBuffers[1]); juce::dsp::ProcessContextReplacing<double> c(b); xover2LP.process(c); }
            { juce::dsp::AudioBlock<double> b(bandBuffers[2]); juce::dsp::ProcessContextReplacing<double> c(b); xover2HP.process(c); }

            // Split at xover3
            for (int ch=0;ch<2;++ch)
                juce::FloatVectorOperations::copy(bandBuffers[3].getWritePointer(ch),bandBuffers[2].getReadPointer(ch),n);
            { juce::dsp::AudioBlock<double> b(bandBuffers[2]); juce::dsp::ProcessContextReplacing<double> c(b); xover3LP.process(c); }
            { juce::dsp::AudioBlock<double> b(bandBuffers[3]); juce::dsp::ProcessContextReplacing<double> c(b); xover3HP.process(c); }
        }

        // Check solo state
        bool anySolo=false;
        for (int bnd=0;bnd<4;++bnd) if(bandParams[bnd].solo.load()) anySolo=true;

        // Clear output
        block.clear();

        // Process and recombine each band
        for (int bnd=0;bnd<4;++bnd)
        {
            if(bandParams[bnd].mute.load()) continue;
            if(anySolo&&!bandParams[bnd].solo.load()) continue;

            double drv=juce::Decibels::decibelsToGain((double)bandParams[bnd].drive.load());
            double out=juce::Decibels::decibelsToGain((double)bandParams[bnd].output.load());
            double bld=bandParams[bnd].blend.load()/100.0;
            int typ=bandParams[bnd].type.load();
            double bitsVal=bandParams[bnd].bits.load();
            double rateVal=bandParams[bnd].rate.load();
            double srRatio = (currentSampleRate > 0) ? rateVal / currentSampleRate : 1.0;

            // Measure band RMS for UI display
            float bandRms = 0.0f;
            for (int ch=0;ch<2;++ch)
            {
                auto* bd=bandBuffers[bnd].getWritePointer(ch);
                auto* dst=block.getChannelPointer(ch);
                for (int i=0;i<n;++i)
                {
                    double dry=bd[i];
                    double input_s = dry;

                    // Sample rate reduction for Bitcrush per-band
                    if (typ == 4 && srRatio < 1.0)
                    {
                        srCounter[bnd] += srRatio;
                        if (srCounter[bnd] >= 1.0) { srCounter[bnd] -= 1.0; srHoldSample[bnd] = input_s; }
                        input_s = srHoldSample[bnd];
                    }

                    double wet=saturateSample(input_s, typ, drv, bitsVal, rateVal)*out;
                    double mixed=dry*(1.0-bld)+wet*bld;
                    dst[i]+=mixed;
                    bandRms += (float)(mixed * mixed);
                }
            }
            bandRms = std::sqrt(bandRms / (float)(n * 2));
            bandRmsLevels[(size_t)bnd].store(juce::Decibels::gainToDecibels(bandRms, -100.0f), std::memory_order_relaxed);
        }
    }

    // Push output to FFT FIFO (mono mix)
    for (int i = 0; i < n; ++i)
    {
        float sample = (float)(block.getSample(0, i) + block.getSample(1, i)) * 0.5f;
        pushSampleToFFT (sample);
    }

    // M/S decode: both channels were processed, just decode
    if (ms > 0)
    {
        auto* ch0 = block.getChannelPointer (0);
        auto* ch1 = block.getChannelPointer (1);
        for (int i = 0; i < n; ++i)
        {
            double m = ch0[i], s = ch1[i];
            ch0[i] = m + s;
            ch1[i] = m - s;
        }
    }

    updateOutputMeters(block);
}

void SaturationStage::reset()
{ xover1LP.reset();xover1HP.reset();xover2LP.reset();xover2HP.reset();xover3LP.reset();xover3HP.reset();
  linXoverLP1.reset();linXoverLP2.reset();linXoverLP3.reset();
  linXoverBuilt = false;
  inputDelayBuf.clear(); inputDelayWP = 0;
  fifoIndex=0; fftReady.store(false); globalSRCounter=0; globalSRHold=0;
  midSRCounter=0; midSRHold=0; sideSRCounter=0; sideSRHold=0;
  for(int i=0;i<4;++i){srCounter[i]=0;srHoldSample[i]=0;}
}

int SaturationStage::getLatencySamples() const
{
    if (!stageOn.load() || mode.load() == 0) return 0;
    if (xoverMode.load() == 1 && linXoverBuilt)
        return LP_DELAY; // parallel approach: all bands at same latency
    return 0;
}

void SaturationStage::rebuildLinearPhaseCrossover()
{
    double sr = currentSampleRate;
    if (sr <= 0) return;
    linXoverLP1.designLowpass ((double) xoverFreq1.load(), sr, 24);
    linXoverLP2.designLowpass ((double) xoverFreq2.load(), sr, 24);
    linXoverLP3.designLowpass ((double) xoverFreq3.load(), sr, 24);
    linXoverBuilt = true;
}

void SaturationStage::pushSampleToFFT (float sample)
{
    fifo[(size_t)fifoIndex] = sample;
    ++fifoIndex;
    if (fifoIndex >= fftSize)
    {
        fifoIndex = 0;
        std::copy (fifo.begin(), fifo.end(), fftData.begin());
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fftReady.store (true, std::memory_order_release);
    }
}

void SaturationStage::computeFFTMagnitudes()
{
    if (! fftReady.load (std::memory_order_acquire))
        return;
    fftReady.store (false, std::memory_order_release);
    fftWindow.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fftProcessor.performFrequencyOnlyForwardTransform (fftData.data());
    auto minDb = -80.0f;
    auto maxDb = 0.0f;
    for (int i = 0; i < fftSize / 2; ++i)
    {
        auto level = juce::Decibels::gainToDecibels (fftData[(size_t)i] / (float) fftSize, minDb);
        magnitudes[(size_t)i] = juce::jmap (level, minDb, maxDb, 0.0f, 1.0f);
    }
}

double SaturationStage::saturateSample (double input, int type, double driveLinear, double bitsVal, double rateVal)
{
    if (type == 3) // Digital — hard clip
        return juce::jlimit (-1.0, 1.0, input * driveLinear);

    if (type == 4) // Bitcrush
    {
        double x = input * driveLinear;
        double lv = std::pow (2.0, bitsVal);
        return std::round (x * lv) / lv;
    }

    // Drive=0dB → pass-through. Drive>0 → progressively more saturation.
    double driveDb = juce::Decibels::gainToDecibels (driveLinear, -100.0);
    if (driveDb < 0.1) return input; // no drive = clean pass-through

    double x = input * driveLinear;
    double saturated;
    switch (type)
    {
        case 0: saturated = std::tanh (x); break;                                              // Tape
        case 1: saturated = (x >= 0) ? (1.0 - std::exp (-x)) : -(1.0 - std::exp (x)) * 0.9; // Tube
                break;
        case 2: saturated = x / (1.0 + std::abs (x)); break;                                  // Transistor
        default: saturated = std::tanh (x); break;
    }

    // Gain compensation: divide by driveLinear to undo the level boost
    saturated /= driveLinear;

    // Crossfade: at low drive, mostly clean. At high drive, mostly saturated.
    double satMix = juce::jlimit (0.0, 1.0, driveDb / 12.0);
    return input * (1.0 - satMix) + saturated * satMix;
}

void SaturationStage::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S4_Sat_On","Sat On",true));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S4_Sat_MS","Channel",juce::StringArray{"Stereo","Mid","Side"},0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S4_Sat_Mode","Mode",juce::StringArray{"Single","Multiband"},0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S4_Sat_Type","Type",juce::StringArray{"Tape","Tube","Transistor","Digital"},0));
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
        layout.add(std::make_unique<juce::AudioParameterChoice>(p+"Type",lb+"Type",juce::StringArray{"Tape","Tube","Transistor","Digital"},0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p+"Drive",lb+"Drive",juce::NormalisableRange<float>(0,24,0.1f),0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p+"Bits",lb+"Bits",juce::NormalisableRange<float>(4,24,1),16));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p+"Rate",lb+"Rate",juce::NormalisableRange<float>(1000,48000,1),44100));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p+"Output",lb+"Out",juce::NormalisableRange<float>(-12,12,0.1f),0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p+"Blend",lb+"Blend",juce::NormalisableRange<float>(0,100,1),100));
        layout.add(std::make_unique<juce::AudioParameterBool>(p+"Solo",lb+"Solo",false));
        layout.add(std::make_unique<juce::AudioParameterBool>(p+"Mute",lb+"Mute",false));
    }
    // ─── Mid single-band params (M/S mode) ───
    layout.add(std::make_unique<juce::AudioParameterChoice>("S4_Sat_M_Type","M Type",juce::StringArray{"Tape","Tube","Transistor","Digital"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_M_Drive","M Drive",juce::NormalisableRange<float>(0,24,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_M_Bits","M Bits",juce::NormalisableRange<float>(4,24,1),16));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_M_Rate","M Rate",juce::NormalisableRange<float>(1000,48000,1),44100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_M_Output","M Output",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_M_Blend","M Blend",juce::NormalisableRange<float>(0,100,1),100));
    // ─── Side single-band params (M/S mode) ───
    layout.add(std::make_unique<juce::AudioParameterChoice>("S4_Sat_S_Type","S Type",juce::StringArray{"Tape","Tube","Transistor","Digital"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_S_Drive","S Drive",juce::NormalisableRange<float>(0,24,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_S_Bits","S Bits",juce::NormalisableRange<float>(4,24,1),16));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_S_Rate","S Rate",juce::NormalisableRange<float>(1000,48000,1),44100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_S_Output","S Output",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S4_Sat_S_Blend","S Blend",juce::NormalisableRange<float>(0,100,1),100));
}

void SaturationStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S4_Sat_On")->load()>0.5f);
    msMode.store((int)a.getRawParameterValue("S4_Sat_MS")->load());
    mode.store((int)a.getRawParameterValue("S4_Sat_Mode")->load());
    satType.store((int)a.getRawParameterValue("S4_Sat_Type")->load());
    drive.store(a.getRawParameterValue("S4_Sat_Drive")->load());
    bits.store(a.getRawParameterValue("S4_Sat_Bits")->load());
    rate.store(a.getRawParameterValue("S4_Sat_Rate")->load());
    output.store(a.getRawParameterValue("S4_Sat_Output")->load());
    blend.store(a.getRawParameterValue("S4_Sat_Blend")->load());
    // Mid single-band params
    mSatType.store((int)a.getRawParameterValue("S4_Sat_M_Type")->load());
    mDrive.store(a.getRawParameterValue("S4_Sat_M_Drive")->load());
    mBits.store(a.getRawParameterValue("S4_Sat_M_Bits")->load());
    mRate.store(a.getRawParameterValue("S4_Sat_M_Rate")->load());
    mOutput.store(a.getRawParameterValue("S4_Sat_M_Output")->load());
    mBlend.store(a.getRawParameterValue("S4_Sat_M_Blend")->load());
    // Side single-band params
    sSatType.store((int)a.getRawParameterValue("S4_Sat_S_Type")->load());
    sDrive.store(a.getRawParameterValue("S4_Sat_S_Drive")->load());
    sBits.store(a.getRawParameterValue("S4_Sat_S_Bits")->load());
    sRate.store(a.getRawParameterValue("S4_Sat_S_Rate")->load());
    sOutput.store(a.getRawParameterValue("S4_Sat_S_Output")->load());
    sBlend.store(a.getRawParameterValue("S4_Sat_S_Blend")->load());
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

    // Rebuild linear phase FIR crossover when mode=linear and freqs change
    // Uses O(N·logN) IFFT kernel design — 340x faster than cosine synthesis
    if (xoverMode.load() == 1 && mode.load() == 1)
    {
        float xf1 = xoverFreq1.load(), xf2 = xoverFreq2.load(), xf3 = xoverFreq3.load();
        bool needRebuild = !linXoverBuilt
            || std::abs (xf1 - lastXF1) > 10.0f
            || std::abs (xf2 - lastXF2) > 10.0f
            || std::abs (xf3 - lastXF3) > 10.0f;
        if (needRebuild)
        {
            rebuildLinearPhaseCrossover();
            lastXF1 = xf1; lastXF2 = xf2; lastXF3 = xf3;
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  OUTPUT EQ STAGE
// ─────────────────────────────────────────────────────────────

void OutputEQStage::prepare(double sr,int bs)
{
    currentSampleRate=sr; currentBlockSize=bs;
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)bs,1};
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        bandL[b].prepare(spec); bandR[b].prepare(spec);
        midBandL[b].prepare(spec);
        sideBandL[b].prepare(spec); sideBandR[b].prepare(spec);
    }
    freq[0].store(100); freq[1].store(400); freq[2].store(1000); freq[3].store(3500); freq[4].store(8000);
    midFreq[0].store(100); midFreq[1].store(400); midFreq[2].store(1000); midFreq[3].store(3500); midFreq[4].store(8000);
    sideFreq[0].store(100); sideFreq[1].store(400); sideFreq[2].store(1000); sideFreq[3].store(3500); sideFreq[4].store(8000);
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        gain[b].store(0); q[b].store(0.707f);
        midGain[b].store(0); midQ[b].store(0.707f);
        sideGain[b].store(0); sideQ[b].store(0.707f);
    }
    updateFilters();
    updateMidFilters();
    updateSideFilters();
}

void OutputEQStage::process(juce::dsp::AudioBlock<double>& block)
{
    if(!stageOn.load())return; updateInputMeters(block);
    int n=(int)block.getNumSamples();
    auto* ch0 = block.getChannelPointer(0);
    auto* ch1 = block.getChannelPointer(1);

    // Step 1: Apply STEREO EQ to L/R
    for (int i = 0; i < n; ++i)
    {
        double L = ch0[i], R = ch1[i];
        for (int b = 0; b < NUM_BANDS; ++b)
        { L = bandL[b].processSample(L); R = bandR[b].processSample(R); }
        ch0[i] = L; ch1[i] = R;
    }

    // Step 2: Encode L/R → Mid/Side
    for (int i = 0; i < n; ++i)
    {
        double mid  = (ch0[i] + ch1[i]) * 0.5;
        double side = (ch0[i] - ch1[i]) * 0.5;
        ch0[i] = mid; ch1[i] = side;
    }

    // Step 3: Apply MID EQ to ch0
    for (int i = 0; i < n; ++i)
    {
        double s = ch0[i];
        for (int b = 0; b < NUM_BANDS; ++b)
            s = midBandL[b].processSample(s);
        ch0[i] = s;
    }

    // Step 4: Apply SIDE EQ to ch1
    for (int i = 0; i < n; ++i)
    {
        double s = ch1[i];
        for (int b = 0; b < NUM_BANDS; ++b)
            s = sideBandL[b].processSample(s);
        ch1[i] = s;
    }

    // Step 5: Decode M/S → L/R
    for (int i = 0; i < n; ++i)
    {
        double m = ch0[i], s = ch1[i];
        ch0[i] = m + s;
        ch1[i] = m - s;
    }

    // FFT
    for (int i = 0; i < n; ++i)
    {
        float sample = (float)(ch0[i] + ch1[i]) * 0.5f;
        pushSampleToFFT (sample);
    }

    updateOutputMeters(block);
}

void OutputEQStage::reset()
{
    for (int b = 0; b < NUM_BANDS; ++b)
    { bandL[b].reset(); bandR[b].reset(); midBandL[b].reset(); sideBandL[b].reset(); sideBandR[b].reset(); }
    fftFifoIndex = 0; fftReady.store(false);
}

void OutputEQStage::updateFilters()
{
    double sr=currentSampleRate; if(sr<=0)return;
    auto ls=juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr,freq[0].load(),(double)q[0].load(),juce::Decibels::decibelsToGain((double)gain[0].load()));
    *bandL[0].coefficients=*ls; *bandR[0].coefficients=*ls;
    for (int b = 1; b <= 3; ++b)
    {
        auto pk=juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr,freq[b].load(),(double)q[b].load(),juce::Decibels::decibelsToGain((double)gain[b].load()));
        *bandL[b].coefficients=*pk; *bandR[b].coefficients=*pk;
    }
    auto hs=juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr,freq[4].load(),(double)q[4].load(),juce::Decibels::decibelsToGain((double)gain[4].load()));
    *bandL[4].coefficients=*hs; *bandR[4].coefficients=*hs;
}

void OutputEQStage::updateSideFilters()
{
    double sr=currentSampleRate; if(sr<=0)return;
    auto ls=juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr,sideFreq[0].load(),(double)sideQ[0].load(),juce::Decibels::decibelsToGain((double)sideGain[0].load()));
    *sideBandL[0].coefficients=*ls; *sideBandR[0].coefficients=*ls;
    for (int b = 1; b <= 3; ++b)
    {
        auto pk=juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr,sideFreq[b].load(),(double)sideQ[b].load(),juce::Decibels::decibelsToGain((double)sideGain[b].load()));
        *sideBandL[b].coefficients=*pk; *sideBandR[b].coefficients=*pk;
    }
    auto hs=juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr,sideFreq[4].load(),(double)sideQ[4].load(),juce::Decibels::decibelsToGain((double)sideGain[4].load()));
    *sideBandL[4].coefficients=*hs; *sideBandR[4].coefficients=*hs;
}

void OutputEQStage::updateMidFilters()
{
    double sr=currentSampleRate; if(sr<=0)return;
    auto ls=juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr,midFreq[0].load(),(double)midQ[0].load(),juce::Decibels::decibelsToGain((double)midGain[0].load()));
    *midBandL[0].coefficients=*ls;
    for (int b = 1; b <= 3; ++b)
    {
        auto pk=juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr,midFreq[b].load(),(double)midQ[b].load(),juce::Decibels::decibelsToGain((double)midGain[b].load()));
        *midBandL[b].coefficients=*pk;
    }
    auto hs2=juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr,midFreq[4].load(),(double)midQ[4].load(),juce::Decibels::decibelsToGain((double)midGain[4].load()));
    *midBandL[4].coefficients=*hs2;
}

double OutputEQStage::getMagnitudeAtFreq (double f) const
{
    double sr = currentSampleRate; if (sr <= 0) return 0.0;
    double mag = 1.0;
    for (int b = 0; b < NUM_BANDS; ++b)
        if (bandL[b].coefficients) mag *= bandL[b].coefficients->getMagnitudeForFrequency (f, sr);
    return juce::Decibels::gainToDecibels (mag, -60.0);
}

double OutputEQStage::getMagnitudeAtFreqSide (double f) const
{
    double sr = currentSampleRate; if (sr <= 0) return 0.0;
    double mag = 1.0;
    for (int b = 0; b < NUM_BANDS; ++b)
        if (sideBandL[b].coefficients) mag *= sideBandL[b].coefficients->getMagnitudeForFrequency (f, sr);
    return juce::Decibels::gainToDecibels (mag, -60.0);
}

OutputEQStage::BandInfo OutputEQStage::getBandInfo (int band) const
{
    int type = (band == 0) ? 0 : (band == 4) ? 2 : 1;
    return { freq[band].load(), gain[band].load(), q[band].load(), type };
}

OutputEQStage::BandInfo OutputEQStage::getBandInfoSide (int band) const
{
    int type = (band == 0) ? 0 : (band == 4) ? 2 : 1;
    return { sideFreq[band].load(), sideGain[band].load(), sideQ[band].load(), type };
}

double OutputEQStage::getMagnitudeAtFreqMid (double f) const
{
    double sr = currentSampleRate; if (sr <= 0) return 0.0;
    double mag = 1.0;
    for (int b = 0; b < NUM_BANDS; ++b)
        if (midBandL[b].coefficients) mag *= midBandL[b].coefficients->getMagnitudeForFrequency (f, sr);
    return juce::Decibels::gainToDecibels (mag, -60.0);
}

OutputEQStage::BandInfo OutputEQStage::getBandInfoMid (int band) const
{
    int type = (band == 0) ? 0 : (band == 4) ? 2 : 1;
    return { midFreq[band].load(), midGain[band].load(), midQ[band].load(), type };
}

void OutputEQStage::pushSampleToFFT (float sample)
{
    fftFifo[(size_t)fftFifoIndex] = sample;
    if (++fftFifoIndex >= fftSize)
    {
        fftFifoIndex = 0;
        std::copy (fftFifo.begin(), fftFifo.end(), fftData.begin());
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fftReady.store (true, std::memory_order_release);
    }
}

void OutputEQStage::computeFFTMagnitudes()
{
    if (!fftReady.load(std::memory_order_acquire)) return;
    fftReady.store(false, std::memory_order_release);
    fftWindow.multiplyWithWindowingTable(fftData.data(), (size_t)fftSize);
    fftProcessor.performFrequencyOnlyForwardTransform(fftData.data());
    for (int i = 0; i < fftSize / 2; ++i)
    {
        auto level = juce::Decibels::gainToDecibels(fftData[(size_t)i] / (float)fftSize, -80.0f);
        fftMagnitudes[(size_t)i] = juce::jmap(level, -80.0f, 0.0f, 0.0f, 1.0f);
    }
}

void OutputEQStage::addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S5_EQ2_On","OutEQ On",true));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S5_EQ2_MS","Channel",juce::StringArray{"Stereo","Mid","Side"},0));
    // Band 0: Low Shelf
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_LowShelf_Freq","LS F",juce::NormalisableRange<float>(20,500,1,0.4f),100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_LowShelf_Gain","LS G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_LowShelf_Q","LS Q",juce::NormalisableRange<float>(0.1f,4,0.01f),0.707f));
    // Band 1: Low-Mid Peak
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_LowMid_Freq","LM F",juce::NormalisableRange<float>(80,2000,1,0.35f),400));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_LowMid_Gain","LM G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_LowMid_Q","LM Q",juce::NormalisableRange<float>(0.1f,10,0.01f),1.0));
    // Band 2: Mid Peak (backward compat with old params)
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_Mid_Freq","Mid F",juce::NormalisableRange<float>(100,10000,1,0.3f),1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_Mid_Gain","Mid G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_Mid_Q","Mid Q",juce::NormalisableRange<float>(0.1f,10,0.01f),1.0));
    // Band 3: High-Mid Peak
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_HighMid_Freq","HM F",juce::NormalisableRange<float>(500,12000,1,0.3f),3500));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_HighMid_Gain","HM G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_HighMid_Q","HM Q",juce::NormalisableRange<float>(0.1f,10,0.01f),1.0));
    // Band 4: High Shelf (backward compat)
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_HighShelf_Freq","HS F",juce::NormalisableRange<float>(1000,16000,1,0.3f),8000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_HighShelf_Gain","HS G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_HighShelf_Q","HS Q",juce::NormalisableRange<float>(0.1f,4,0.01f),0.707f));
    // ─── Side channel EQ bands (used in M/S mode) ───
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_LS_Freq","S LS F",juce::NormalisableRange<float>(20,500,1,0.4f),100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_LS_Gain","S LS G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_LS_Q","S LS Q",juce::NormalisableRange<float>(0.1f,4,0.01f),0.707f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_LM_Freq","S LM F",juce::NormalisableRange<float>(80,2000,1,0.35f),400));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_LM_Gain","S LM G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_LM_Q","S LM Q",juce::NormalisableRange<float>(0.1f,10,0.01f),1.0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_Mid_Freq","S Mid F",juce::NormalisableRange<float>(100,10000,1,0.3f),1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_Mid_Gain","S Mid G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_Mid_Q","S Mid Q",juce::NormalisableRange<float>(0.1f,10,0.01f),1.0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_HM_Freq","S HM F",juce::NormalisableRange<float>(500,12000,1,0.3f),3500));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_HM_Gain","S HM G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_HM_Q","S HM Q",juce::NormalisableRange<float>(0.1f,10,0.01f),1.0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_HS_Freq","S HS F",juce::NormalisableRange<float>(1000,16000,1,0.3f),8000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_HS_Gain","S HS G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_S_HS_Q","S HS Q",juce::NormalisableRange<float>(0.1f,4,0.01f),0.707f));
    // ─── Mid channel EQ bands ───
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_LS_Freq","M LS F",juce::NormalisableRange<float>(20,500,1,0.4f),100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_LS_Gain","M LS G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_LS_Q","M LS Q",juce::NormalisableRange<float>(0.1f,4,0.01f),0.707f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_LM_Freq","M LM F",juce::NormalisableRange<float>(80,2000,1,0.35f),400));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_LM_Gain","M LM G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_LM_Q","M LM Q",juce::NormalisableRange<float>(0.1f,10,0.01f),1.0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_Mid_Freq","M Mid F",juce::NormalisableRange<float>(100,10000,1,0.3f),1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_Mid_Gain","M Mid G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_Mid_Q","M Mid Q",juce::NormalisableRange<float>(0.1f,10,0.01f),1.0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_HM_Freq","M HM F",juce::NormalisableRange<float>(500,12000,1,0.3f),3500));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_HM_Gain","M HM G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_HM_Q","M HM Q",juce::NormalisableRange<float>(0.1f,10,0.01f),1.0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_HS_Freq","M HS F",juce::NormalisableRange<float>(1000,16000,1,0.3f),8000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_HS_Gain","M HS G",juce::NormalisableRange<float>(-12,12,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S5_EQ2_M_HS_Q","M HS Q",juce::NormalisableRange<float>(0.1f,4,0.01f),0.707f));
}

void OutputEQStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S5_EQ2_On")->load()>0.5f);
    msMode.store((int)a.getRawParameterValue("S5_EQ2_MS")->load());
    freq[0].store(a.getRawParameterValue("S5_EQ2_LowShelf_Freq")->load());
    gain[0].store(a.getRawParameterValue("S5_EQ2_LowShelf_Gain")->load());
    q[0].store(a.getRawParameterValue("S5_EQ2_LowShelf_Q")->load());
    freq[1].store(a.getRawParameterValue("S5_EQ2_LowMid_Freq")->load());
    gain[1].store(a.getRawParameterValue("S5_EQ2_LowMid_Gain")->load());
    q[1].store(a.getRawParameterValue("S5_EQ2_LowMid_Q")->load());
    freq[2].store(a.getRawParameterValue("S5_EQ2_Mid_Freq")->load());
    gain[2].store(a.getRawParameterValue("S5_EQ2_Mid_Gain")->load());
    q[2].store(a.getRawParameterValue("S5_EQ2_Mid_Q")->load());
    freq[3].store(a.getRawParameterValue("S5_EQ2_HighMid_Freq")->load());
    gain[3].store(a.getRawParameterValue("S5_EQ2_HighMid_Gain")->load());
    q[3].store(a.getRawParameterValue("S5_EQ2_HighMid_Q")->load());
    freq[4].store(a.getRawParameterValue("S5_EQ2_HighShelf_Freq")->load());
    gain[4].store(a.getRawParameterValue("S5_EQ2_HighShelf_Gain")->load());
    q[4].store(a.getRawParameterValue("S5_EQ2_HighShelf_Q")->load());
    // Side params
    sideFreq[0].store(a.getRawParameterValue("S5_EQ2_S_LS_Freq")->load());
    sideGain[0].store(a.getRawParameterValue("S5_EQ2_S_LS_Gain")->load());
    sideQ[0].store(a.getRawParameterValue("S5_EQ2_S_LS_Q")->load());
    sideFreq[1].store(a.getRawParameterValue("S5_EQ2_S_LM_Freq")->load());
    sideGain[1].store(a.getRawParameterValue("S5_EQ2_S_LM_Gain")->load());
    sideQ[1].store(a.getRawParameterValue("S5_EQ2_S_LM_Q")->load());
    sideFreq[2].store(a.getRawParameterValue("S5_EQ2_S_Mid_Freq")->load());
    sideGain[2].store(a.getRawParameterValue("S5_EQ2_S_Mid_Gain")->load());
    sideQ[2].store(a.getRawParameterValue("S5_EQ2_S_Mid_Q")->load());
    sideFreq[3].store(a.getRawParameterValue("S5_EQ2_S_HM_Freq")->load());
    sideGain[3].store(a.getRawParameterValue("S5_EQ2_S_HM_Gain")->load());
    sideQ[3].store(a.getRawParameterValue("S5_EQ2_S_HM_Q")->load());
    sideFreq[4].store(a.getRawParameterValue("S5_EQ2_S_HS_Freq")->load());
    sideGain[4].store(a.getRawParameterValue("S5_EQ2_S_HS_Gain")->load());
    sideQ[4].store(a.getRawParameterValue("S5_EQ2_S_HS_Q")->load());
    // Mid params
    midFreq[0].store(a.getRawParameterValue("S5_EQ2_M_LS_Freq")->load());
    midGain[0].store(a.getRawParameterValue("S5_EQ2_M_LS_Gain")->load());
    midQ[0].store(a.getRawParameterValue("S5_EQ2_M_LS_Q")->load());
    midFreq[1].store(a.getRawParameterValue("S5_EQ2_M_LM_Freq")->load());
    midGain[1].store(a.getRawParameterValue("S5_EQ2_M_LM_Gain")->load());
    midQ[1].store(a.getRawParameterValue("S5_EQ2_M_LM_Q")->load());
    midFreq[2].store(a.getRawParameterValue("S5_EQ2_M_Mid_Freq")->load());
    midGain[2].store(a.getRawParameterValue("S5_EQ2_M_Mid_Gain")->load());
    midQ[2].store(a.getRawParameterValue("S5_EQ2_M_Mid_Q")->load());
    midFreq[3].store(a.getRawParameterValue("S5_EQ2_M_HM_Freq")->load());
    midGain[3].store(a.getRawParameterValue("S5_EQ2_M_HM_Gain")->load());
    midQ[3].store(a.getRawParameterValue("S5_EQ2_M_HM_Q")->load());
    midFreq[4].store(a.getRawParameterValue("S5_EQ2_M_HS_Freq")->load());
    midGain[4].store(a.getRawParameterValue("S5_EQ2_M_HS_Gain")->load());
    midQ[4].store(a.getRawParameterValue("S5_EQ2_M_HS_Q")->load());
    updateFilters();
    updateMidFilters();
    updateSideFilters();
}

// ─────────────────────────────────────────────────────────────
//  FILTER STAGE
// ─────────────────────────────────────────────────────────────

void FilterStage::prepare(double sr,int bs)
{
    currentSampleRate=sr;currentBlockSize=bs;
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)bs,1};
    for(int i=0;i<MAX_STAGES;++i){hpL[i].prepare(spec);hpR[i].prepare(spec);lpL[i].prepare(spec);lpR[i].prepare(spec);}
    for(int i=0;i<MAX_STAGES;++i){hpMid[i].prepare(spec);lpMid[i].prepare(spec);hpSide[i].prepare(spec);lpSide[i].prepare(spec);}
    linPhaseHP.prepare(sr,bs); linPhaseLP.prepare(sr,bs);
    // M/S FIRs not needed — M/S always uses IIR (minimum phase)
    linPhaseBuilt = false;
    lastHPFreq = -1; lastLPFreq = -1; lastHPSlope = -1; lastLPSlope = -1;
    lastHPOn = false; lastLPOn = false;
    msDelayMid.assign ((size_t) MAX_LINPHASE_DELAY, 0.0);
    msDelaySide.assign ((size_t) MAX_LINPHASE_DELAY, 0.0);
    msDelayMidWP = 0; msDelaySideWP = 0;
    updateFilters(); updateMidFilters(); updateSideFilters();
}

void FilterStage::process(juce::dsp::AudioBlock<double>& block)
{
    if(!stageOn.load())return; updateInputMeters(block);

    bool isLinear = (filterMode.load() == 1);
    int ms = msMode.load();
    int n = (int)block.getNumSamples();

    if (ms > 0)
    {
        // ─── M/S DUAL: always uses IIR (minimum phase) ───
        // Linear phase in M/S would require 4 FIR instances + delay compensation
        // — same approach as FabFilter Pro-Q: M/S = minimum phase only
        auto* ch0 = block.getChannelPointer(0);
        auto* ch1 = block.getChannelPointer(1);
        for (int i = 0; i < n; ++i)
        {
            double mid  = (ch0[i] + ch1[i]) * 0.5;
            double side = (ch0[i] - ch1[i]) * 0.5;
            ch0[i] = mid; ch1[i] = side;
        }

        static const int stageMap[] = { 1, 1, 2, 2, 4 };
        int mHpS = mHpOn.load() ? stageMap[juce::jlimit(0,4,(int)mHpSlope.load())] : 0;
        int mLpS = mLpOn.load() ? stageMap[juce::jlimit(0,4,(int)mLpSlope.load())] : 0;
        int sHpS = sHpOn.load() ? stageMap[juce::jlimit(0,4,(int)sHpSlope.load())] : 0;
        int sLpS = sLpOn.load() ? stageMap[juce::jlimit(0,4,(int)sLpSlope.load())] : 0;
        for (int i = 0; i < n; ++i)
        {
            double sM = ch0[i], sS = ch1[i];
            for(int s=0;s<mHpS&&s<MAX_STAGES;++s) sM=hpMid[s].processSample(sM);
            for(int s=0;s<mLpS&&s<MAX_STAGES;++s) sM=lpMid[s].processSample(sM);
            for(int s=0;s<sHpS&&s<MAX_STAGES;++s) sS=hpSide[s].processSample(sS);
            for(int s=0;s<sLpS&&s<MAX_STAGES;++s) sS=lpSide[s].processSample(sS);
            ch0[i]=sM; ch1[i]=sS;
        }

        // Decode M/S → L/R
        for (int i = 0; i < n; ++i)
        {
            double m = ch0[i], s = ch1[i];
            ch0[i] = m + s; ch1[i] = m - s;
        }
    }
    else
    {
        // ─── STEREO ───
        if (isLinear && linPhaseBuilt)
        {
            if (hpOn.load() && linPhaseHP.isActive()) linPhaseHP.process(block);
            if (lpOn.load() && linPhaseLP.isActive()) linPhaseLP.process(block);
        }
        else if (!isLinear)
        {
            static const int stageMap[] = { 1, 1, 2, 2, 4 };
            auto*l=block.getChannelPointer(0); auto*r=block.getChannelPointer(1);
            int hpS = hpOn.load() ? stageMap[juce::jlimit(0,4,(int)hpSlope.load())] : 0;
            int lpS = lpOn.load() ? stageMap[juce::jlimit(0,4,(int)lpSlope.load())] : 0;
            for(int i=0;i<n;++i)
            {
                double sL=l[i],sR=r[i];
                for(int s=0;s<hpS&&s<MAX_STAGES;++s){sL=hpL[s].processSample(sL);sR=hpR[s].processSample(sR);}
                for(int s=0;s<lpS&&s<MAX_STAGES;++s){sL=lpL[s].processSample(sL);sR=lpR[s].processSample(sR);}
                l[i]=sL; r[i]=sR;
            }
        }
    }
    updateOutputMeters(block);

    // Push output to FFT
    auto* outL = block.getChannelPointer(0);
    auto* outR = block.getChannelPointer(1);
    for (int i = 0; i < n; ++i)
        pushSampleToFFT ((float)((outL[i] + outR[i]) * 0.5));
}

void FilterStage::reset()
{
    for(int i=0;i<MAX_STAGES;++i){hpL[i].reset();hpR[i].reset();lpL[i].reset();lpR[i].reset();}
    for(int i=0;i<MAX_STAGES;++i){hpMid[i].reset();lpMid[i].reset();hpSide[i].reset();lpSide[i].reset();}
    linPhaseHP.reset(); linPhaseLP.reset();
    linPhaseHPMid.reset(); linPhaseLPMid.reset();
    linPhaseHPSide.reset(); linPhaseLPSide.reset();
    linPhaseBuilt = false;
    std::fill(msDelayMid.begin(), msDelayMid.end(), 0.0);
    std::fill(msDelaySide.begin(), msDelaySide.end(), 0.0);
    msDelayMidWP = 0; msDelaySideWP = 0;
    fftFifoIndex = 0; fftReady.store (false);
}

void FilterStage::pushSampleToFFT (float sample)
{
    fftFifo[(size_t) fftFifoIndex] = sample;
    if (++fftFifoIndex >= fftSize)
    {
        fftFifoIndex = 0;
        std::copy (fftFifo.begin(), fftFifo.end(), fftData.begin());
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fftReady.store (true, std::memory_order_release);
    }
}

void FilterStage::computeFFTMagnitudes()
{
    if (!fftReady.load (std::memory_order_acquire)) return;
    fftReady.store (false, std::memory_order_release);
    fftWindow.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fftProcessor.performFrequencyOnlyForwardTransform (fftData.data());
    for (int i = 0; i < fftSize / 2; ++i)
    {
        auto level = juce::Decibels::gainToDecibels (fftData[(size_t)i] / (float) fftSize, -80.0f);
        float val = juce::jmap (level, -80.0f, 0.0f, 0.0f, 1.0f);
        fftMagnitudes[(size_t)i] = fftMagnitudes[(size_t)i] * 0.8f + val * 0.2f;
    }
}

int FilterStage::getLatencySamples()const
{
    if (!stageOn.load()) return 0;
    // Linear phase only in Stereo mode
    if (filterMode.load() == 1 && linPhaseBuilt && msMode.load() == 0)
    {
        int lat = 0;
        if (hpOn.load() && linPhaseHP.isActive()) lat += LinearPhaseFIR::FIR_SIZE / 2;
        if (lpOn.load() && linPhaseLP.isActive()) lat += LinearPhaseFIR::FIR_SIZE / 2;
        return lat;
    }
    return 0;
}

void FilterStage::rebuildLinearPhase()
{
    double sr = currentSampleRate;
    if (sr <= 0) return;
    static const int slopeDbMap[] = { 6, 12, 18, 24, 48 };

    // Linear phase only in Stereo mode — M/S always uses IIR
    if (hpOn.load())
        linPhaseHP.designHighpass ((double)hpFreq.load(), sr, slopeDbMap[juce::jlimit(0,4,(int)hpSlope.load())]);
    else
        linPhaseHP.reset();

    if (lpOn.load())
        linPhaseLP.designLowpass ((double)lpFreq.load(), sr, slopeDbMap[juce::jlimit(0,4,(int)lpSlope.load())]);
    else
        linPhaseLP.reset();

    linPhaseBuilt = true;
}

void FilterStage::updateFilters()
{
    double sr=currentSampleRate;if(sr<=0)return;
    int hpSlopeIdx = juce::jlimit(0,4,(int)hpSlope.load());
    int lpSlopeIdx = juce::jlimit(0,4,(int)lpSlope.load());
    for(int i=0;i<MAX_STAGES;++i)
    {
        if (hpSlopeIdx==0&&i==0) { auto h=juce::dsp::IIR::Coefficients<double>::makeFirstOrderHighPass(sr,(double)hpFreq.load()); *hpL[i].coefficients=*h;*hpR[i].coefficients=*h; }
        else { auto h=juce::dsp::IIR::Coefficients<double>::makeHighPass(sr,(double)hpFreq.load(),0.707); *hpL[i].coefficients=*h;*hpR[i].coefficients=*h; }
        if (lpSlopeIdx==0&&i==0) { auto lo=juce::dsp::IIR::Coefficients<double>::makeFirstOrderLowPass(sr,(double)lpFreq.load()); *lpL[i].coefficients=*lo;*lpR[i].coefficients=*lo; }
        else { auto lo=juce::dsp::IIR::Coefficients<double>::makeLowPass(sr,(double)lpFreq.load(),0.707); *lpL[i].coefficients=*lo;*lpR[i].coefficients=*lo; }
    }
}

void FilterStage::updateMidFilters()
{
    double sr=currentSampleRate;if(sr<=0)return;
    int hpSI=juce::jlimit(0,4,(int)mHpSlope.load());
    int lpSI=juce::jlimit(0,4,(int)mLpSlope.load());
    for(int i=0;i<MAX_STAGES;++i)
    {
        if(hpSI==0&&i==0){auto h=juce::dsp::IIR::Coefficients<double>::makeFirstOrderHighPass(sr,(double)mHpFreq.load());*hpMid[i].coefficients=*h;}
        else{auto h=juce::dsp::IIR::Coefficients<double>::makeHighPass(sr,(double)mHpFreq.load(),0.707);*hpMid[i].coefficients=*h;}
        if(lpSI==0&&i==0){auto lo=juce::dsp::IIR::Coefficients<double>::makeFirstOrderLowPass(sr,(double)mLpFreq.load());*lpMid[i].coefficients=*lo;}
        else{auto lo=juce::dsp::IIR::Coefficients<double>::makeLowPass(sr,(double)mLpFreq.load(),0.707);*lpMid[i].coefficients=*lo;}
    }
}

void FilterStage::updateSideFilters()
{
    double sr=currentSampleRate;if(sr<=0)return;
    int hpSI=juce::jlimit(0,4,(int)sHpSlope.load());
    int lpSI=juce::jlimit(0,4,(int)sLpSlope.load());
    for(int i=0;i<MAX_STAGES;++i)
    {
        if(hpSI==0&&i==0){auto h=juce::dsp::IIR::Coefficients<double>::makeFirstOrderHighPass(sr,(double)sHpFreq.load());*hpSide[i].coefficients=*h;}
        else{auto h=juce::dsp::IIR::Coefficients<double>::makeHighPass(sr,(double)sHpFreq.load(),0.707);*hpSide[i].coefficients=*h;}
        if(lpSI==0&&i==0){auto lo=juce::dsp::IIR::Coefficients<double>::makeFirstOrderLowPass(sr,(double)sLpFreq.load());*lpSide[i].coefficients=*lo;}
        else{auto lo=juce::dsp::IIR::Coefficients<double>::makeLowPass(sr,(double)sLpFreq.load(),0.707);*lpSide[i].coefficients=*lo;}
    }
}

void FilterStage::addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S6_Filter_On","Filter On",true));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6_Filter_MS","Channel",juce::StringArray{"Stereo","Mid","Side"},0));
    // Stereo params
    layout.add(std::make_unique<juce::AudioParameterBool>("S6_HP_On","HP On",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6_HP_Freq","HP F",juce::NormalisableRange<float>(20,2000,0.1f,0.4f),30));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6_HP_Slope","HP Slope",juce::StringArray{"6","12","18","24","48"},1));
    layout.add(std::make_unique<juce::AudioParameterBool>("S6_LP_On","LP On",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6_LP_Freq","LP F",juce::NormalisableRange<float>(200,20000,1.0f,0.3f),18000));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6_LP_Slope","LP Slope",juce::StringArray{"6","12","18","24","48"},1));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6_Filter_Mode","Flt Mode",juce::StringArray{"Min Phase","Linear Phase"},0));
    // Mid params
    layout.add(std::make_unique<juce::AudioParameterBool>("S6_HP_M_On","M HP On",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6_HP_M_Freq","M HP F",juce::NormalisableRange<float>(20,2000,0.1f,0.4f),30));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6_HP_M_Slope","M HP Slope",juce::StringArray{"6","12","18","24","48"},1));
    layout.add(std::make_unique<juce::AudioParameterBool>("S6_LP_M_On","M LP On",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6_LP_M_Freq","M LP F",juce::NormalisableRange<float>(200,20000,1.0f,0.3f),18000));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6_LP_M_Slope","M LP Slope",juce::StringArray{"6","12","18","24","48"},1));
    // Side params
    layout.add(std::make_unique<juce::AudioParameterBool>("S6_HP_S_On","S HP On",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6_HP_S_Freq","S HP F",juce::NormalisableRange<float>(20,2000,0.1f,0.4f),30));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6_HP_S_Slope","S HP Slope",juce::StringArray{"6","12","18","24","48"},1));
    layout.add(std::make_unique<juce::AudioParameterBool>("S6_LP_S_On","S LP On",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6_LP_S_Freq","S LP F",juce::NormalisableRange<float>(200,20000,1.0f,0.3f),18000));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6_LP_S_Slope","S LP Slope",juce::StringArray{"6","12","18","24","48"},1));
}

void FilterStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S6_Filter_On")->load()>0.5f);
    msMode.store((int)a.getRawParameterValue("S6_Filter_MS")->load());
    // Stereo
    bool curHPOn = a.getRawParameterValue("S6_HP_On")->load()>0.5f;
    float curHP = a.getRawParameterValue("S6_HP_Freq")->load();
    int curHPS = (int)a.getRawParameterValue("S6_HP_Slope")->load();
    bool curLPOn = a.getRawParameterValue("S6_LP_On")->load()>0.5f;
    float curLP = a.getRawParameterValue("S6_LP_Freq")->load();
    int curLPS = (int)a.getRawParameterValue("S6_LP_Slope")->load();
    int newMode = (int)a.getRawParameterValue("S6_Filter_Mode")->load();
    hpOn.store(curHPOn); hpFreq.store(curHP); hpSlope.store(curHPS);
    lpOn.store(curLPOn); lpFreq.store(curLP); lpSlope.store(curLPS);
    filterMode.store(newMode);
    updateFilters();
    // Mid
    mHpOn.store(a.getRawParameterValue("S6_HP_M_On")->load()>0.5f);
    mHpFreq.store(a.getRawParameterValue("S6_HP_M_Freq")->load());
    mHpSlope.store((int)a.getRawParameterValue("S6_HP_M_Slope")->load());
    mLpOn.store(a.getRawParameterValue("S6_LP_M_On")->load()>0.5f);
    mLpFreq.store(a.getRawParameterValue("S6_LP_M_Freq")->load());
    mLpSlope.store((int)a.getRawParameterValue("S6_LP_M_Slope")->load());
    updateMidFilters();
    // Side
    sHpOn.store(a.getRawParameterValue("S6_HP_S_On")->load()>0.5f);
    sHpFreq.store(a.getRawParameterValue("S6_HP_S_Freq")->load());
    sHpSlope.store((int)a.getRawParameterValue("S6_HP_S_Slope")->load());
    sLpOn.store(a.getRawParameterValue("S6_LP_S_On")->load()>0.5f);
    sLpFreq.store(a.getRawParameterValue("S6_LP_S_Freq")->load());
    sLpSlope.store((int)a.getRawParameterValue("S6_LP_S_Slope")->load());
    updateSideFilters();

    // Rebuild linear phase FIR — check ALL params (stereo + M/S)
    if (newMode == 1)
    {
        int curMs = msMode.load();
        static int lastMs = -1;

        // Always rebuild when: first time, mode switch, msMode change, or any param change
        bool needRebuild = !linPhaseBuilt || curMs != lastMs
            || curHPOn != lastHPOn || curLPOn != lastLPOn
            || std::abs (curHP - lastHPFreq) > 5.0f
            || std::abs (curLP - lastLPFreq) > 5.0f
            || curHPS != lastHPSlope || curLPS != lastLPSlope;

        // Also check M/S params when in M/S mode
        if (!needRebuild && curMs > 0)
            needRebuild = true; // always rebuild in M/S mode (simpler, rebuild is cheap)

        if (needRebuild)
        {
            rebuildLinearPhase();
            lastHPFreq = curHP; lastLPFreq = curLP;
            lastHPSlope = curHPS; lastLPSlope = curLPS;
            lastHPOn = curHPOn; lastLPOn = curLPOn;
            lastMs = curMs;
        }
    }
    else if (linPhaseBuilt)
    {
        // Switched back to min phase — clear FIR state
        linPhaseHP.reset(); linPhaseLP.reset();
        linPhaseBuilt = false;
    }
}

// ─────────────────────────────────────────────────────────────
//  DYNAMIC RESONANCE — FFT analysis + IIR processing (robust)
// ─────────────────────────────────────────────────────────────

void DynamicResonanceStage::prepare(double sr, int bs)
{
    currentSampleRate = sr;
    currentBlockSize = bs;

    // Set up 24 bands logarithmically from 200Hz to 16kHz
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        float t = (float)i / (float)(NUM_BANDS - 1);
        bands[(size_t)i].centerFreq = 200.0 * std::pow(80.0, (double)t);  // 200Hz to 16kHz
        bands[(size_t)i].envelope = 0.0;
        bands[(size_t)i].avgMag = 0.0;
        bands[(size_t)i].currentGainDb = 0.0;
    }

    juce::dsp::ProcessSpec spec { sr, (juce::uint32)bs, 1 };
    for (auto& band : bands)
    {
        band.filterL.prepare(spec);
        band.filterR.prepare(spec);
        auto c = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, band.centerFreq, 4.0, 1.0);
        *band.filterL.coefficients = *c;
        *band.filterR.coefficients = *c;
    }

    // Analysis buffer (mono, collects samples for FFT)
    collectBuffer.setSize(1, FFT_SIZE);
    collectBuffer.clear();
    collectPos = 0;

    // Hann window for analysis
    for (int i = 0; i < FFT_SIZE; ++i)
        analysisWindow[(size_t)i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * (float)i / (float)(FFT_SIZE - 1)));
}

void DynamicResonanceStage::process(juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters(block);

    float depthAmt = depth.load();
    if (depthAmt < 0.1f)
    {
        // Reset band gains smoothly
        for (auto& band : bands)
            band.currentGainDb *= 0.95;
        updateOutputMeters(block);
        return;
    }

    int n = (int)block.getNumSamples();
    auto* left = block.getChannelPointer(0);
    auto* right = block.getChannelPointer(1);

    // Collect mono samples for FFT analysis
    auto* collectData = collectBuffer.getWritePointer(0);
    for (int i = 0; i < n; ++i)
    {
        collectData[collectPos] = (float)((left[i] + right[i]) * 0.5);
        collectPos++;
        if (collectPos >= FFT_SIZE)
        {
            collectPos = 0;
            analyzeSpectrum();
            updateBandFilters();
        }
    }

    // Apply IIR filters to audio (this always works cleanly)
    for (auto& band : bands)
    {
        if (std::abs(band.currentGainDb) < 0.1)
            continue;  // Skip bands with negligible GR

        for (int i = 0; i < n; ++i)
        {
            left[i]  = band.filterL.processSample(left[i]);
            right[i] = band.filterR.processSample(right[i]);
        }
    }

    updateOutputMeters(block);
}

void DynamicResonanceStage::analyzeSpectrum()
{
    // Copy collected audio to FFT buffer and apply window
    auto* src = collectBuffer.getReadPointer(0);
    for (int i = 0; i < FFT_SIZE; ++i)
        analysisBuffer[(size_t)i] = src[i] * analysisWindow[(size_t)i];
    for (int i = FFT_SIZE; i < FFT_SIZE * 2; ++i)
        analysisBuffer[(size_t)i] = 0.0f;

    // Forward FFT
    analysisFft.performRealOnlyForwardTransform(analysisBuffer.data());

    // Extract magnitudes
    int halfSize = FFT_SIZE / 2;
    for (int i = 0; i <= halfSize; ++i)
    {
        float re = analysisBuffer[(size_t)(i * 2)];
        float im = analysisBuffer[(size_t)(i * 2 + 1)];
        spectrum[(size_t)i] = std::sqrt(re * re + im * im);
    }

    // For each band, compute magnitude at center frequency vs local average
    float sharpVal = sharpness.load() / 100.0f;
    int windowSize = juce::jmax(2, (int)((1.0f - sharpVal) * 40.0f) + 2);

    float selectAmt = selectivity.load() / 100.0f;
    float threshold = 1.2f + (1.0f - selectAmt) * 3.0f;

    float depthScale = depth.load() / 100.0f;
    float speedVal = speed.load() / 100.0f;
    float attackRate = 0.2f + speedVal * 0.6f;
    float releaseRate = 0.02f + speedVal * 0.08f;

    float loFreq = lowFreq.load();
    float mdFreq = midFreq.load();
    float hiFreq = highFreq.load();
    float loD = lowDepth.load() / 100.0f;
    float mdD = midDepth.load() / 100.0f;
    float hiD = highDepth.load() / 100.0f;

    for (int b = 0; b < NUM_BANDS; ++b)
    {
        float freq = (float)bands[(size_t)b].centerFreq;

        // Skip bands outside range
        if (freq < loFreq || freq > hiFreq)
        {
            bands[(size_t)b].currentGainDb *= 0.9;
            bandGR[(size_t)b].store((float)bands[(size_t)b].currentGainDb, std::memory_order_relaxed);
            continue;
        }

        // Per-band depth scaling: Low / Mid / High
        float bandDepthMul = (freq < mdFreq) ? loD : (freq < hiFreq * 0.7f) ? mdD : hiD;

        int centerBin = freqToBin(freq);

        // Get magnitude at center
        float centerMag = spectrum[(size_t)centerBin];

        // Get average magnitude in surrounding region
        float avgMag = 0.0f;
        int count = 0;
        for (int j = juce::jmax(1, centerBin - windowSize); j <= juce::jmin(halfSize, centerBin + windowSize); ++j)
        {
            avgMag += spectrum[(size_t)j];
            count++;
        }
        avgMag = (count > 0) ? avgMag / (float)count : centerMag;

        // Detect resonance: center magnitude vs local average
        float targetGainDb = 0.0f;
        if (avgMag > 1e-8f)
        {
            float ratio = centerMag / avgMag;
            if (ratio > threshold)
            {
                float excessDb = 20.0f * std::log10(ratio / threshold);
                // Mode: Soft = gentle, Hard = aggressive (2x depth multiplier)
                float modeMultiplier = (dynMode.load() == 0) ? 1.0f : 2.0f;
                targetGainDb = -excessDb * depthScale * bandDepthMul * modeMultiplier;
                float maxCut = (dynMode.load() == 0) ? -6.0f : -18.0f;
                targetGainDb = juce::jmax(targetGainDb, maxCut);
            }
        }

        // Smooth gain change
        float current = (float)bands[(size_t)b].currentGainDb;
        if (targetGainDb < current)
            current += attackRate * (targetGainDb - current);
        else
            current += releaseRate * (targetGainDb - current);

        bands[(size_t)b].currentGainDb = (double)current;
        bandGR[(size_t)b].store(current, std::memory_order_relaxed);
    }
}

void DynamicResonanceStage::updateBandFilters()
{
    float sharpVal = sharpness.load() / 100.0f;
    double Q = 2.0 + sharpVal * 12.0;  // Q from 2 (wide) to 14 (narrow)

    for (int b = 0; b < NUM_BANDS; ++b)
    {
        double gainDb = bands[(size_t)b].currentGainDb;
        if (std::abs(gainDb) < 0.1)
            gainDb = 0.0;  // Snap to zero for negligible amounts

        double gainLinear = juce::Decibels::decibelsToGain(gainDb);
        auto coeffs = juce::dsp::IIR::Coefficients<double>::makePeakFilter(
            currentSampleRate, bands[(size_t)b].centerFreq, Q, gainLinear);
        *bands[(size_t)b].filterL.coefficients = *coeffs;
        *bands[(size_t)b].filterR.coefficients = *coeffs;
    }
}

int DynamicResonanceStage::freqToBin(float freq) const
{
    return juce::jlimit(1, FFT_SIZE / 2 - 1,
        (int)(freq * (float)FFT_SIZE / (float)currentSampleRate));
}

float DynamicResonanceStage::getBandFreq(int band) const
{
    if (band < 0 || band >= NUM_BANDS) return 0.0f;
    return (float)bands[(size_t)band].centerFreq;
}

void DynamicResonanceStage::reset()
{
    for (auto& band : bands)
    {
        band.filterL.reset();
        band.filterR.reset();
        band.envelope = 0.0;
        band.avgMag = 0.0;
        band.currentGainDb = 0.0;
    }
    collectBuffer.clear();
    collectPos = 0;
    analysisBuffer.fill(0);
    spectrum.fill(0);
    for (auto& gr : bandGR)
        gr.store(0.0f, std::memory_order_relaxed);
}

void DynamicResonanceStage::addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S6B_DynEQ_On","DynRes On",false));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6B_DynEQ_Mode","Mode",juce::StringArray{"Soft","Hard"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_Depth","Depth",juce::NormalisableRange<float>(0,100,1),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_Sensitivity","Selectivity",juce::NormalisableRange<float>(0,100,1),50));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_Sharpness","Sharpness",juce::NormalisableRange<float>(0,100,1),50));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_Speed","Speed",juce::NormalisableRange<float>(0,100,1),50));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_LowFreq","Low Freq",juce::NormalisableRange<float>(20,10000,1,0.3f),200));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_MidFreq","Mid Freq",juce::NormalisableRange<float>(200,15000,1,0.3f),2000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_HighFreq","High Freq",juce::NormalisableRange<float>(1000,20000,1,0.3f),12000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_LowDepth","Low Depth",juce::NormalisableRange<float>(0,200,1),100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_MidDepth","Mid Depth",juce::NormalisableRange<float>(0,200,1),100));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_HighDepth","High Depth",juce::NormalisableRange<float>(0,200,1),100));
}

void DynamicResonanceStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S6B_DynEQ_On")->load()>0.5f);
    dynMode.store((int)a.getRawParameterValue("S6B_DynEQ_Mode")->load());
    depth.store(a.getRawParameterValue("S6B_DynEQ_Depth")->load());
    selectivity.store(a.getRawParameterValue("S6B_DynEQ_Sensitivity")->load());
    sharpness.store(a.getRawParameterValue("S6B_DynEQ_Sharpness")->load());
    speed.store(a.getRawParameterValue("S6B_DynEQ_Speed")->load());
    lowFreq.store(a.getRawParameterValue("S6B_DynEQ_LowFreq")->load());
    midFreq.store(a.getRawParameterValue("S6B_DynEQ_MidFreq")->load());
    highFreq.store(a.getRawParameterValue("S6B_DynEQ_HighFreq")->load());
    lowDepth.store(a.getRawParameterValue("S6B_DynEQ_LowDepth")->load());
    midDepth.store(a.getRawParameterValue("S6B_DynEQ_MidDepth")->load());
    highDepth.store(a.getRawParameterValue("S6B_DynEQ_HighDepth")->load());
}

// ─────────────────────────────────────────────────────────────
//  DYNAMIC EQ STAGE — 5-band parametric with per-band dynamics + M/S
// ─────────────────────────────────────────────────────────────

void DynamicEQStage::prepare(double sr, int bs)
{
    currentSampleRate = sr; currentBlockSize = bs;
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)bs,1};
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        bandL[b].prepare(spec); bandR[b].prepare(spec);
        midBandM[b].prepare(spec);
        sideBandM[b].prepare(spec);
        scBpL[b].prepare(spec); scBpR[b].prepare(spec);
    }
    float defFreqs[] = {100,400,1000,3500,8000};
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        freq[b].store(defFreqs[b]); gain[b].store(0); q[b].store(0.707f);
        midFreq[b].store(defFreqs[b]); midGain[b].store(0); midQ[b].store(0.707f);
        sideFreq[b].store(defFreqs[b]); sideGain[b].store(0); sideQ[b].store(0.707f);
        bandOn[b].store(true);
        dynThreshold[b].store(-20.0f); dynRange[b].store(-12.0f);
        dynRatio[b].store(4.0f); dynAttack[b].store(10.0f); dynRelease[b].store(100.0f);
        envState[b] = 0.0; smoothedDynGain[b] = 0.0;
        dynamicGainDb[b].store(0.0f); bandGRDisplay[b].store(0.0f);
    }
    updateFilters(); updateMidFilters(); updateSideFilters(); updateScFilters();
}

void DynamicEQStage::process(juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters(block);
    int n = (int)block.getNumSamples();
    auto* ch0 = block.getChannelPointer(0);
    auto* ch1 = block.getChannelPointer(1);
    double sr = currentSampleRate;
    if (sr <= 0) sr = 44100.0;

    // ─── Cache all atomic values ONCE per block ───
    bool bOn[NUM_BANDS];
    double cachedThresh[NUM_BANDS], cachedRange[NUM_BANDS], cachedRatio[NUM_BANDS];
    double attCoeff[NUM_BANDS], relCoeff[NUM_BANDS];
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        bOn[b] = bandOn[b].load();
        cachedThresh[b] = dynThreshold[b].load();
        cachedRange[b] = dynRange[b].load();
        cachedRatio[b] = dynRatio[b].load();
        double attMs = juce::jmax(0.1, (double)dynAttack[b].load());
        double relMs = juce::jmax(1.0, (double)dynRelease[b].load());
        attCoeff[b] = std::exp(-1.0 / (sr * attMs * 0.001));
        relCoeff[b] = std::exp(-1.0 / (sr * relMs * 0.001));
    }
    double dynSmoothCoeff = std::exp(-1.0 / (sr * 0.002)); // 2ms smooth

    // ─── SUB-BLOCK PROCESSING for fast dynamic response ───
    int pos = 0;
    while (pos < n)
    {
        int subN = juce::jmin(SUB_BLOCK_SIZE, n - pos);

        // 1) Envelope detection (per sample within sub-block)
        for (int i = pos; i < pos + subN; ++i)
        {
            double inL = ch0[i], inR = ch1[i];
            for (int b = 0; b < NUM_BANDS; ++b)
            {
                if (!bOn[b]) continue;
                double scL = scBpL[b].processSample(inL);
                double scR = scBpR[b].processSample(inR);
                double scPeak = std::max(std::abs(scL), std::abs(scR));
                double coeff = (scPeak > envState[b]) ? (1.0 - attCoeff[b]) : (1.0 - relCoeff[b]);
                envState[b] += coeff * (scPeak - envState[b]);
            }
        }

        // 2) Compute dynamic gain per band (once per sub-block)
        for (int b = 0; b < NUM_BANDS; ++b)
        {
            if (!bOn[b]) { smoothedDynGain[b] *= dynSmoothCoeff; continue; }
            double envDb = juce::Decibels::gainToDecibels(envState[b], -100.0);
            double dynGainDb_val = 0.0;
            if (envDb > cachedThresh[b])
            {
                double over = envDb - cachedThresh[b];
                double ratio = juce::jmax(1.0, cachedRatio[b]);
                double compressed = over * (1.0 - 1.0 / ratio);
                double range = cachedRange[b];
                if (range < 0) dynGainDb_val = -juce::jlimit(0.0, -range, compressed);
                else           dynGainDb_val =  juce::jlimit(0.0,  range, compressed);
            }
            smoothedDynGain[b] = smoothedDynGain[b] * dynSmoothCoeff + dynGainDb_val * (1.0 - dynSmoothCoeff);
        }

        // 3) Update filter coefficients (once per sub-block — fast response!)
        updateFilters();
        updateMidFilters();
        updateSideFilters();

        // 4) Apply Stereo EQ to L/R
        for (int i = pos; i < pos + subN; ++i)
        {
            double L = ch0[i], R = ch1[i];
            for (int b = 0; b < NUM_BANDS; ++b)
            {
                if (!bOn[b]) continue;
                L = bandL[b].processSample(L);
                R = bandR[b].processSample(R);
            }
            ch0[i] = L; ch1[i] = R;
        }

        // 5) M/S encode → apply Mid EQ + Side EQ → M/S decode
        for (int i = pos; i < pos + subN; ++i)
        {
            double mid  = (ch0[i] + ch1[i]) * 0.5;
            double side = (ch0[i] - ch1[i]) * 0.5;
            // Mid EQ
            for (int b = 0; b < NUM_BANDS; ++b)
            {
                if (!bOn[b]) continue;
                mid = midBandM[b].processSample(mid);
            }
            // Side EQ
            for (int b = 0; b < NUM_BANDS; ++b)
            {
                if (!bOn[b]) continue;
                side = sideBandM[b].processSample(side);
            }
            ch0[i] = mid + side;
            ch1[i] = mid - side;
        }

        pos += subN;
    }

    // Store UI meters (once per block)
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        dynamicGainDb[b].store((float)smoothedDynGain[b]);
        bandGRDisplay[b].store((float)smoothedDynGain[b]);
    }

    // FFT push
    for (int i = 0; i < n; ++i)
        pushSampleToFFT((float)(ch0[i] + ch1[i]) * 0.5f);
    updateOutputMeters(block);
}

void DynamicEQStage::reset()
{
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        bandL[b].reset(); bandR[b].reset();
        midBandM[b].reset(); sideBandM[b].reset();
        scBpL[b].reset(); scBpR[b].reset();
        envState[b] = 0.0; smoothedDynGain[b] = 0.0;
    }
    fftFifoIndex = 0; fftReady.store(false);
}

void DynamicEQStage::updateFilters()
{
    double sr = currentSampleRate; if (sr <= 0) return;
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        if (!bandOn[b].load()) continue;
        double f = freq[b].load();
        double g = gain[b].load() + smoothedDynGain[b];
        double qv = q[b].load();
        g = juce::jlimit(-24.0, 24.0, g);
        double linGain = juce::Decibels::decibelsToGain(g);
        juce::ReferenceCountedObjectPtr<juce::dsp::IIR::Coefficients<double>> c;
        if (b == 0)      c = juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr, f, qv, linGain);
        else if (b == 4) c = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, f, qv, linGain);
        else             c = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, f, qv, linGain);
        *bandL[b].coefficients = *c; *bandR[b].coefficients = *c;
    }
}

void DynamicEQStage::updateMidFilters()
{
    double sr = currentSampleRate; if (sr <= 0) return;
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        double f = midFreq[b].load();
        double g = midGain[b].load(); // Mid/Side: no dynamic gain (shared dynamics only on stereo)
        double qv = midQ[b].load();
        g = juce::jlimit(-24.0, 24.0, g);
        double linGain = juce::Decibels::decibelsToGain(g);
        juce::ReferenceCountedObjectPtr<juce::dsp::IIR::Coefficients<double>> c;
        if (b == 0)      c = juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr, f, qv, linGain);
        else if (b == 4) c = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, f, qv, linGain);
        else             c = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, f, qv, linGain);
        *midBandM[b].coefficients = *c;
    }
}

void DynamicEQStage::updateSideFilters()
{
    double sr = currentSampleRate; if (sr <= 0) return;
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        double f = sideFreq[b].load();
        double g = sideGain[b].load();
        double qv = sideQ[b].load();
        g = juce::jlimit(-24.0, 24.0, g);
        double linGain = juce::Decibels::decibelsToGain(g);
        juce::ReferenceCountedObjectPtr<juce::dsp::IIR::Coefficients<double>> c;
        if (b == 0)      c = juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr, f, qv, linGain);
        else if (b == 4) c = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, f, qv, linGain);
        else             c = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, f, qv, linGain);
        *sideBandM[b].coefficients = *c;
    }
}

void DynamicEQStage::updateScFilters()
{
    double sr = currentSampleRate; if (sr <= 0) return;
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        double f = freq[b].load();
        double scQ = (b == 0 || b == 4) ? 0.7 : 1.5;
        auto c = juce::dsp::IIR::Coefficients<double>::makeBandPass(sr, f, scQ);
        *scBpL[b].coefficients = *c; *scBpR[b].coefficients = *c;
    }
}

double DynamicEQStage::getMagnitudeAtFreq(double f) const
{
    double mag = 0.0;
    double sr = currentSampleRate; if (sr <= 0) sr = 44100;
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        if (!bandOn[b].load()) continue;
        double g = gain[b].load() + dynamicGainDb[b].load();
        g = juce::jlimit(-24.0, 24.0, g);
        juce::ReferenceCountedObjectPtr<juce::dsp::IIR::Coefficients<double>> c;
        if (b == 0)      c = juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr, freq[b].load(), q[b].load(), juce::Decibels::decibelsToGain(g));
        else if (b == 4) c = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, freq[b].load(), q[b].load(), juce::Decibels::decibelsToGain(g));
        else             c = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, freq[b].load(), q[b].load(), juce::Decibels::decibelsToGain(g));
        mag += juce::Decibels::gainToDecibels(c->getMagnitudeForFrequency(f, sr));
    }
    return mag;
}

double DynamicEQStage::getMagnitudeAtFreqMid(double f) const
{
    double mag = 0.0;
    double sr = currentSampleRate; if (sr <= 0) sr = 44100;
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        double g = midGain[b].load();
        g = juce::jlimit(-24.0, 24.0, g);
        juce::ReferenceCountedObjectPtr<juce::dsp::IIR::Coefficients<double>> c;
        if (b == 0)      c = juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr, midFreq[b].load(), midQ[b].load(), juce::Decibels::decibelsToGain(g));
        else if (b == 4) c = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, midFreq[b].load(), midQ[b].load(), juce::Decibels::decibelsToGain(g));
        else             c = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, midFreq[b].load(), midQ[b].load(), juce::Decibels::decibelsToGain(g));
        mag += juce::Decibels::gainToDecibels(c->getMagnitudeForFrequency(f, sr));
    }
    return mag;
}

double DynamicEQStage::getMagnitudeAtFreqSide(double f) const
{
    double mag = 0.0;
    double sr = currentSampleRate; if (sr <= 0) sr = 44100;
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        double g = sideGain[b].load();
        g = juce::jlimit(-24.0, 24.0, g);
        juce::ReferenceCountedObjectPtr<juce::dsp::IIR::Coefficients<double>> c;
        if (b == 0)      c = juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr, sideFreq[b].load(), sideQ[b].load(), juce::Decibels::decibelsToGain(g));
        else if (b == 4) c = juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr, sideFreq[b].load(), sideQ[b].load(), juce::Decibels::decibelsToGain(g));
        else             c = juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr, sideFreq[b].load(), sideQ[b].load(), juce::Decibels::decibelsToGain(g));
        mag += juce::Decibels::gainToDecibels(c->getMagnitudeForFrequency(f, sr));
    }
    return mag;
}

DynamicEQStage::BandInfo DynamicEQStage::getBandInfo(int band) const
{
    int type = (band == 0) ? 0 : (band == 4) ? 2 : 1;
    return { freq[band].load(), gain[band].load() + dynamicGainDb[band].load(), q[band].load(), type, bandOn[band].load() };
}

DynamicEQStage::BandInfo DynamicEQStage::getBandInfoMid(int band) const
{
    int type = (band == 0) ? 0 : (band == 4) ? 2 : 1;
    return { midFreq[band].load(), midGain[band].load(), midQ[band].load(), type, bandOn[band].load() };
}

DynamicEQStage::BandInfo DynamicEQStage::getBandInfoSide(int band) const
{
    int type = (band == 0) ? 0 : (band == 4) ? 2 : 1;
    return { sideFreq[band].load(), sideGain[band].load(), sideQ[band].load(), type, bandOn[band].load() };
}

DynamicEQStage::DynInfo DynamicEQStage::getDynInfo(int band) const
{
    return { dynThreshold[band].load(), dynRange[band].load(), dynRatio[band].load(),
             dynAttack[band].load(), dynRelease[band].load() };
}

void DynamicEQStage::pushSampleToFFT(float sample)
{
    fftFifo[(size_t)fftFifoIndex] = sample;
    if (++fftFifoIndex >= fftSize)
    {
        fftFifoIndex = 0;
        std::copy(fftFifo.begin(), fftFifo.end(), fftData.begin());
        std::fill(fftData.begin() + fftSize, fftData.end(), 0.0f);
        fftReady.store(true, std::memory_order_release);
    }
}

void DynamicEQStage::computeFFTMagnitudes()
{
    if (!fftReady.load(std::memory_order_acquire)) return;
    fftReady.store(false, std::memory_order_release);
    fftWindow.multiplyWithWindowingTable(fftData.data(), (size_t)fftSize);
    fftProcessor.performFrequencyOnlyForwardTransform(fftData.data());
    for (int i = 0; i < fftSize / 2; ++i)
    {
        auto level = juce::Decibels::gainToDecibels(fftData[(size_t)i] / (float)fftSize, -80.0f);
        fftMagnitudes[(size_t)i] = juce::jmap(level, -80.0f, 0.0f, 0.0f, 1.0f);
    }
}

void DynamicEQStage::addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S6C_DEQ_On","DynEQ On",true));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S6C_DEQ_MS","Channel",juce::StringArray{"Stereo","Mid","Side"},0));
    juce::String bandNames[] = { "LS", "LM", "Mid", "HM", "HS" };
    float defFreqs[] = { 100.0f, 400.0f, 1000.0f, 3500.0f, 8000.0f };
    float freqMin[] = { 20.0f, 80.0f, 100.0f, 500.0f, 1000.0f };
    float freqMax[] = { 500.0f, 2000.0f, 10000.0f, 12000.0f, 16000.0f };
    float qMin[] = { 0.1f, 0.1f, 0.1f, 0.1f, 0.1f };
    float qMax[] = { 4.0f, 10.0f, 10.0f, 10.0f, 4.0f };
    float qDef[] = { 0.707f, 1.0f, 1.0f, 1.0f, 0.707f };

    for (int b = 0; b < 5; ++b)
    {
        auto pfx = "S6C_DEQ_B" + juce::String(b) + "_";
        // Shared dynamic params
        layout.add(std::make_unique<juce::AudioParameterBool>(pfx+"On", bandNames[b]+" On", true));
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"Thresh", bandNames[b]+" Thr",
            juce::NormalisableRange<float>(-60, 0, 0.1f), -6));
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"Range", bandNames[b]+" Rng",
            juce::NormalisableRange<float>(-24, 24, 0.1f), -12));
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"Ratio", bandNames[b]+" Rat",
            juce::NormalisableRange<float>(1, 20, 0.1f), 4));
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"Attack", bandNames[b]+" Att",
            juce::NormalisableRange<float>(0.1f, 200, 0.1f, 0.4f), 10));
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"Release", bandNames[b]+" Rel",
            juce::NormalisableRange<float>(1, 2000, 1, 0.35f), 100));
        // Stereo EQ params
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"Freq", bandNames[b]+" F",
            juce::NormalisableRange<float>(freqMin[b], freqMax[b], 1, 0.3f), defFreqs[b]));
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"Gain", bandNames[b]+" G",
            juce::NormalisableRange<float>(-24, 24, 0.1f), 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"Q", bandNames[b]+" Q",
            juce::NormalisableRange<float>(qMin[b], qMax[b], 0.01f), qDef[b]));
        // Mid EQ params
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"M_Freq", "M "+bandNames[b]+" F",
            juce::NormalisableRange<float>(freqMin[b], freqMax[b], 1, 0.3f), defFreqs[b]));
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"M_Gain", "M "+bandNames[b]+" G",
            juce::NormalisableRange<float>(-24, 24, 0.1f), 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"M_Q", "M "+bandNames[b]+" Q",
            juce::NormalisableRange<float>(qMin[b], qMax[b], 0.01f), qDef[b]));
        // Side EQ params
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"S_Freq", "S "+bandNames[b]+" F",
            juce::NormalisableRange<float>(freqMin[b], freqMax[b], 1, 0.3f), defFreqs[b]));
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"S_Gain", "S "+bandNames[b]+" G",
            juce::NormalisableRange<float>(-24, 24, 0.1f), 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(pfx+"S_Q", "S "+bandNames[b]+" Q",
            juce::NormalisableRange<float>(qMin[b], qMax[b], 0.01f), qDef[b]));
    }
}

void DynamicEQStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S6C_DEQ_On")->load() > 0.5f);
    msMode.store((int)a.getRawParameterValue("S6C_DEQ_MS")->load());
    for (int b = 0; b < NUM_BANDS; ++b)
    {
        auto pfx = "S6C_DEQ_B" + juce::String(b) + "_";
        bandOn[b].store(a.getRawParameterValue(pfx+"On")->load() > 0.5f);
        // Shared dynamics
        dynThreshold[b].store(a.getRawParameterValue(pfx+"Thresh")->load());
        dynRange[b].store(a.getRawParameterValue(pfx+"Range")->load());
        dynRatio[b].store(a.getRawParameterValue(pfx+"Ratio")->load());
        dynAttack[b].store(a.getRawParameterValue(pfx+"Attack")->load());
        dynRelease[b].store(a.getRawParameterValue(pfx+"Release")->load());
        // Stereo
        freq[b].store(a.getRawParameterValue(pfx+"Freq")->load());
        gain[b].store(a.getRawParameterValue(pfx+"Gain")->load());
        q[b].store(a.getRawParameterValue(pfx+"Q")->load());
        // Mid
        midFreq[b].store(a.getRawParameterValue(pfx+"M_Freq")->load());
        midGain[b].store(a.getRawParameterValue(pfx+"M_Gain")->load());
        midQ[b].store(a.getRawParameterValue(pfx+"M_Q")->load());
        // Side
        sideFreq[b].store(a.getRawParameterValue(pfx+"S_Freq")->load());
        sideGain[b].store(a.getRawParameterValue(pfx+"S_Gain")->load());
        sideQ[b].store(a.getRawParameterValue(pfx+"S_Q")->load());
    }
    updateScFilters();
}

// ─────────────────────────────────────────────────────────────
//  CLIPPER STAGE
// ─────────────────────────────────────────────────────────────

void ClipperStage::prepare (double sr, int bs)
{
    currentSampleRate = sr; currentBlockSize = bs;
    fastEnvL = 1e-6; fastEnvR = 1e-6;
    slowEnvL = 1e-6; slowEnvR = 1e-6;

    // SPL Transient Designer approach: TWO smooth envelopes
    // Fast: tracks peaks with ~1ms attack, ~15ms release
    // Slow: tracks body with ~20ms attack, ~200ms release
    // Difference = transient component (already smooth, no discontinuities)
    fastAttack  = std::exp (-1.0 / (sr * 0.001));   // 1ms attack
    fastRelease = std::exp (-1.0 / (sr * 0.015));   // 15ms release
    slowAttack  = std::exp (-1.0 / (sr * 0.020));   // 20ms attack
    slowRelease = std::exp (-1.0 / (sr * 0.200));   // 200ms release

    clipOS = std::make_unique<juce::dsp::Oversampling<double>> (2, 1,
        juce::dsp::Oversampling<double>::filterHalfBandPolyphaseIIR, true);
    clipOS->initProcessing ((juce::uint32) bs);
    osReady = true;
    waveWritePos.store (0);
}

void ClipperStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters (block);

    int n = (int) block.getNumSamples();
    double inG  = juce::Decibels::decibelsToGain ((double) inputGain.load());
    double outG = juce::Decibels::decibelsToGain ((double) outputGain.load());
    double cl   = juce::Decibels::decibelsToGain ((double) ceiling.load());
    double shp  = shape.load() / 100.0;
    double transPct = transient.load() / 100.0;
    int mode = clipMode.load();
    double wet = mixPct.load() / 100.0;
    double dry = 1.0 - wet;

    // Apply input gain
    if (std::abs (inG - 1.0) > 1e-6) block.multiplyBy (inG);

    // Measure input peak
    double inPeak = 0;
    for (int ch = 0; ch < (int) block.getNumChannels() && ch < 2; ++ch)
    {
        auto* d = block.getChannelPointer (ch);
        for (int i = 0; i < n; ++i)
            inPeak = std::max (inPeak, std::abs (d[i]));
    }
    inputPeakDb.store ((float) juce::Decibels::gainToDecibels (inPeak, -100.0));

    // Capture input waveform
    int wp = waveWritePos.load();
    {
        auto* l = block.getChannelPointer (0);
        auto* r = block.getChannelPointer (1);
        for (int i = 0; i < n; ++i)
        {
            waveIn[(size_t) (wp % WAVE_BUF_SIZE)] = (float)((l[i] + r[i]) * 0.5);
            wp++;
        }
    }

    // ─── TRANSIENT SHAPER — SPL Transient Designer approach ───
    //
    // Two SMOOTH envelopes (both with attack+release, no instant jumps):
    //   Fast: 1ms attack, 15ms release — follows peaks closely
    //   Slow: 20ms attack, 200ms release — follows the body/sustain
    //
    // The DIFFERENCE (fast - slow) is naturally smooth and represents
    // the transient component. This difference is used as a gain
    // multiplier. Because both envelopes are smooth, the gain curve
    // has no discontinuities → zero distortion, zero intermodulation.
    //
    // No headroom limiter needed — the clipper after this handles
    // any peaks that exceed the ceiling. That's the whole point:
    // boost the transient, then let the clipper shape it.

    if (transPct > 0.01)
    {
        auto* l = block.getChannelPointer (0);
        auto* r = block.getChannelPointer (1);

        for (int i = 0; i < n; ++i)
        {
            double absIn = std::max (std::abs (l[i]), std::abs (r[i]));

            if (absIn > fastEnvL)
                fastEnvL = fastAttack * fastEnvL + (1.0 - fastAttack) * absIn;
            else
                fastEnvL = fastRelease * fastEnvL + (1.0 - fastRelease) * absIn;

            if (absIn > slowEnvL)
                slowEnvL = slowAttack * slowEnvL + (1.0 - slowAttack) * absIn;
            else
                slowEnvL = slowRelease * slowEnvL + (1.0 - slowRelease) * absIn;

            double gain = 1.0;
            if (slowEnvL > 1e-8 && fastEnvL > slowEnvL)
            {
                double ratio = fastEnvL / slowEnvL;
                double boostLinear = 1.0 + (ratio - 1.0) * transPct * 1.0;
                boostLinear = juce::jlimit (1.0, 1.5, boostLinear); // max +3.5 dB
                gain = boostLinear;
            }

            // Apply gain but NEVER exceed ceiling
            l[i] = juce::jlimit (-cl, cl, l[i] * gain);
            r[i] = juce::jlimit (-cl, cl, r[i] * gain);
        }
    }

    // ─── CLIPPING with oversampling ───
    bool isClipOnly = (mode >= 4);
    int clipIters = (mode == 5) ? 3 : 1; // ClipOnly3 = 3 iterations, ClipOnly2 = 1
    if (osReady && clipOS)
    {
        auto osBlock = clipOS->processSamplesUp (block);
        int osN = (int) osBlock.getNumSamples();

        if (isClipOnly)
        {
            // ClipOnly2/3: per-channel slew-limited clipping
            for (int ch = 0; ch < (int) osBlock.getNumChannels() && ch < 2; ++ch)
            {
                auto* d = osBlock.getChannelPointer (ch);
                auto& lastS = (ch == 0) ? lastClipL : lastClipR;
                for (int i = 0; i < osN; ++i)
                {
                    double clipped = clipOnly3Sample (d[i], cl, lastS, clipIters);
                    d[i] = juce::jlimit (-cl, cl, clipped * outG);
                }
            }
        }
        else
        {
            for (int ch = 0; ch < (int) osBlock.getNumChannels() && ch < 2; ++ch)
            {
                auto* d = osBlock.getChannelPointer (ch);
                for (int i = 0; i < osN; ++i)
                {
                    double input = d[i];
                    double clipped = clipSample (input, cl, shp, mode);
                    double mixed = input * dry + clipped * wet;
                    d[i] = juce::jlimit (-cl, cl, mixed * outG);
                }
            }
        }
        clipOS->processSamplesDown (block);

        // Post-downsample hard clip
        for (int ch = 0; ch < (int) block.getNumChannels() && ch < 2; ++ch)
        {
            auto* d = block.getChannelPointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = juce::jlimit (-cl, cl, d[i]);
        }
    }
    else
    {
        if (isClipOnly)
        {
            for (int ch = 0; ch < (int) block.getNumChannels() && ch < 2; ++ch)
            {
                auto* d = block.getChannelPointer (ch);
                auto& lastS = (ch == 0) ? lastClipL : lastClipR;
                for (int i = 0; i < n; ++i)
                {
                    double clipped = clipOnly3Sample (d[i], cl, lastS, clipIters);
                    d[i] = juce::jlimit (-cl, cl, clipped * outG);
                }
            }
        }
        else
        {
            for (int ch = 0; ch < (int) block.getNumChannels() && ch < 2; ++ch)
            {
                auto* d = block.getChannelPointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    double input = d[i];
                    double clipped = clipSample (input, cl, shp, mode);
                    double mixed = input * dry + clipped * wet;
                    d[i] = juce::jlimit (-cl, cl, mixed * outG);
                }
            }
        }
    }

    // Capture output waveform + measure output peak
    double outPeak = 0;
    {
        int wp2 = waveWritePos.load(); // same start pos as input
        auto* l = block.getChannelPointer (0);
        auto* r = block.getChannelPointer (1);
        for (int i = 0; i < n; ++i)
        {
            float mono = (float)((l[i] + r[i]) * 0.5);
            waveOut[(size_t) (wp2 % WAVE_BUF_SIZE)] = mono;
            outPeak = std::max (outPeak, (double) std::abs (mono));
            wp2++;
        }
        waveWritePos.store (wp2 % WAVE_BUF_SIZE);
    }

    outputPeakDb.store ((float) juce::Decibels::gainToDecibels (outPeak, -100.0));
    // Clip amount = difference between input peak and ceiling
    float clipAmt = inputPeakDb.load() - (float) ceiling.load();
    clipAmountDb.store (std::max (0.0f, clipAmt));

    // Write to clip history (scrolling display)
    int chp = clipHistoryPos.load (std::memory_order_relaxed);
    clipHistory[(size_t) chp] = std::max (0.0f, clipAmt);
    clipHistoryPos.store ((chp + 1) % CLIP_HISTORY_SIZE, std::memory_order_relaxed);

    updateOutputMeters (block);
}

void ClipperStage::reset()
{
    fastEnvL = 1e-6; fastEnvR = 1e-6;
    slowEnvL = 1e-6; slowEnvR = 1e-6;
    lastClipL = 0.0; lastClipR = 0.0;
    if (clipOS) clipOS->reset();
    waveWritePos.store (0);
    waveIn.fill (0); waveOut.fill (0);
}

double ClipperStage::clipSample (double input, double ceilLin, double shapeFactor, int mode)
{
    double x = input / ceilLin;
    double ax = std::abs (x);
    double sign = (x >= 0) ? 1.0 : -1.0;

    // Hard clip — always the baseline (shape=0 on all modes)
    double hardClipped = juce::jlimit (-1.0, 1.0, x);

    // If shape is zero, ALL modes = clean hard clip
    if (shapeFactor < 0.01)
        return hardClipped * ceilLin;

    // Shaped clip — mode determines character, shape controls amount
    double shaped;
    switch (mode)
    {
        case 0: // HARD — always hard clip regardless of shape
            shaped = hardClipped;
            break;

        case 1: // SOFT — cubic knee, shape controls knee width
        {
            double knee = 1.0 - shapeFactor * 0.5; // 1.0 at shape=0, 0.5 at shape=100
            if (ax <= knee)
                shaped = x;
            else
            {
                double t = juce::jlimit (0.0, 1.0, (ax - knee) / (1.0 - knee + 0.001));
                double compressed = knee + (1.0 - knee) * t * (2.0 - t);
                shaped = sign * juce::jmin (compressed, 1.0);
            }
            break;
        }
        case 2: // ANALOG — exponential approach (tube character, slight asymmetry)
        {
            double threshold = 1.0 - shapeFactor * 0.3; // 1.0 at shape=0, 0.7 at shape=100
            if (ax <= threshold)
                shaped = x;
            else
            {
                double over = (ax - threshold) / (1.0 - threshold + 0.001);
                // Exponential approach to 1.0 — reaches ceiling fast like SOFT
                double y = 1.0 - (1.0 - threshold) * std::exp (-3.0 * over);
                double asym = 1.0 + sign * shapeFactor * 0.01;
                shaped = sign * juce::jmin (y * asym, 1.0);
            }
            break;
        }
        case 3: // WARM — sigmoid approach (tape character, symmetric)
        {
            double threshold = 1.0 - shapeFactor * 0.25; // 1.0 at shape=0, 0.75 at shape=100
            if (ax <= threshold)
                shaped = x;
            else
            {
                double over = (ax - threshold) / (1.0 - threshold + 0.001);
                // Sigmoid approach — reaches ceiling, slightly softer knee than ANALOG
                double y = 1.0 - (1.0 - threshold) * std::exp (-2.5 * over);
                shaped = sign * juce::jmin (y, 1.0);
            }
            break;
        }
        default:
            shaped = hardClipped;
            break;
    }

    // Blend between hard clip and shaped clip based on shape amount
    double y = hardClipped * (1.0 - shapeFactor) + shaped * shapeFactor;
    return juce::jlimit (-ceilLin, ceilLin, y * ceilLin);
}

// ─── Airwindows ClipOnly3 — slew-limited transparent clipper (MIT license) ───
// Based on Chris Johnson's ClipOnly (airwindows.com)
// Limiting the SLEW RATE at the clip point eliminates harsh HF harmonics.
double ClipperStage::clipOnly3Sample (double input, double ceilLin, double& lastSample, int iterations)
{
    double refClip = ceilLin;
    double hardness = 0.7390851332151606; // Airwindows golden ratio constant
    double sample = input;

    // Slew limiting proportional to ceiling
    double slewAmount = refClip * 0.5;

    for (int iter = 0; iter < iterations; ++iter)
    {
        double difference = sample - lastSample;
        if (difference > slewAmount) difference = slewAmount;
        else if (difference < -slewAmount) difference = -slewAmount;
        sample = lastSample + difference;

        double overshoot = std::abs (sample) - refClip;
        if (overshoot > 0.0)
        {
            double softKnee = refClip + overshoot * (1.0 - hardness) / (1.0 + overshoot);
            double blended = refClip * hardness + softKnee * (1.0 - hardness);
            sample = (sample > 0.0) ? blended : -blended;
            sample = juce::jlimit (-refClip, refClip, sample);
        }
    }

    lastSample = sample;
    return sample;
}

float ClipperStage::getTransferCurve (float inputDb)
{
    double cl = juce::Decibels::decibelsToGain ((double) ceiling.load());
    double inLin = juce::Decibels::decibelsToGain ((double) inputDb);
    double shp = shape.load() / 100.0;
    int mode = clipMode.load();
    // ClipOnly modes are stateful (slew-limited) — show hard clip curve for UI
    if (mode >= 4) mode = 0;
    double outLin = clipSample (inLin, cl, shp, mode);
    return (float) juce::Decibels::gainToDecibels (std::abs (outLin), -60.0);
}

void ClipperStage::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add (std::make_unique<juce::AudioParameterBool> ("S7_Clipper_On", "Clipper On", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("S7_Clipper_Input", "Input",
        juce::NormalisableRange<float> (0, 24, 0.1f), 0, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("S7_Clipper_Ceiling", "Ceiling",
        juce::NormalisableRange<float> (-12, 0, 0.1f), -0.3f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("S7_Clipper_Shape", "Shape",
        juce::NormalisableRange<float> (0, 100, 1), 50));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("S7_Clipper_Transient", "Transient",
        juce::NormalisableRange<float> (0, 100, 1), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> ("S7_Clipper_Style", "Mode",
        juce::StringArray { "Hard", "Soft", "Analog", "Warm", "ClipOnly2", "ClipOnly3" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("S7_Clipper_Output", "Output",
        juce::NormalisableRange<float> (-12, 0, 0.1f), 0, juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("S7_Clipper_Mix", "Mix",
        juce::NormalisableRange<float> (0, 100, 1), 100));
}

void ClipperStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store (a.getRawParameterValue ("S7_Clipper_On")->load() > 0.5f);
    inputGain.store (a.getRawParameterValue ("S7_Clipper_Input")->load());
    ceiling.store (a.getRawParameterValue ("S7_Clipper_Ceiling")->load());
    shape.store (a.getRawParameterValue ("S7_Clipper_Shape")->load());
    transient.store (a.getRawParameterValue ("S7_Clipper_Transient")->load());
    clipMode.store ((int) a.getRawParameterValue ("S7_Clipper_Style")->load());
    outputGain.store (a.getRawParameterValue ("S7_Clipper_Output")->load());
    mixPct.store (a.getRawParameterValue ("S7_Clipper_Mix")->load());
}

// ─────────────────────────────────────────────────────────────
//  MULTIBAND DYNAMICS STAGE — Pro-MB style
// ─────────────────────────────────────────────────────────────

void MultibandDynamicsStage::prepare (double sr, int bs)
{
    currentSampleRate = sr; currentBlockSize = bs;
    xover1LP.setCutoffFrequency ((float) xoverFreq1.load()); xover1LP.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    xover1HP.setCutoffFrequency ((float) xoverFreq1.load()); xover1HP.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    xover2LP.setCutoffFrequency ((float) xoverFreq2.load()); xover2LP.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    xover2HP.setCutoffFrequency ((float) xoverFreq2.load()); xover2HP.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    xover3LP.setCutoffFrequency ((float) xoverFreq3.load()); xover3LP.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    xover3HP.setCutoffFrequency ((float) xoverFreq3.load()); xover3HP.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 2 };
    xover1LP.prepare (spec); xover1HP.prepare (spec);
    xover2LP.prepare (spec); xover2HP.prepare (spec);
    xover3LP.prepare (spec); xover3HP.prepare (spec);
    for (auto& b : bandBuffers) b.setSize (2, bs);
    tempBuffer.setSize (2, bs);
    // Linear phase crossover
    linXoverLP1.prepare (sr, bs);
    linXoverLP2.prepare (sr, bs);
    linXoverLP3.prepare (sr, bs);
    linXoverBuilt = false; lastXF1 = -1; lastXF2 = -1; lastXF3 = -1;
    lp1Buf.setSize (2, bs); lp2Buf.setSize (2, bs); lp3Buf.setSize (2, bs);
    inputDelayBuf.setSize (2, LP_DELAY + bs + 1); inputDelayBuf.clear(); inputDelayWP = 0;
    // Reset envelopes
    for (auto& s : bandStateL) { s.envelope = 0; s.gainSmoothed = 1.0; }
    for (auto& s : bandStateR) { s.envelope = 0; s.gainSmoothed = 1.0; }
}

void MultibandDynamicsStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters (block);
    int n = (int) block.getNumSamples();

    // M/S encode
    int ms = msMode.load();
    if (ms > 0)
    {
        auto* ch0 = block.getChannelPointer (0);
        auto* ch1 = block.getChannelPointer (1);
        for (int i = 0; i < n; ++i)
        {
            double l = ch0[i], r = ch1[i];
            ch0[i] = (l + r) * 0.5; // Mid
            ch1[i] = (l - r) * 0.5; // Side
        }
    }

    // ─── IIR Crossover split (always — linear phase path is separate) ───
    bool useLinearPhase = (xoverMode.load() == 1) && linXoverBuilt;

    for (auto& buf : bandBuffers) buf.setSize (2, n, false, false, true);
    tempBuffer.setSize (2, n, false, false, true);

    if (useLinearPhase)
    {
        lp1Buf.setSize (2, n, false, false, true);
        lp2Buf.setSize (2, n, false, false, true);
        lp3Buf.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* src = block.getChannelPointer (ch);
            juce::FloatVectorOperations::copy (lp1Buf.getWritePointer (ch), src, n);
            juce::FloatVectorOperations::copy (lp2Buf.getWritePointer (ch), src, n);
            juce::FloatVectorOperations::copy (lp3Buf.getWritePointer (ch), src, n);
        }
        { juce::dsp::AudioBlock<double> b (lp1Buf); linXoverLP1.process (b); }
        { juce::dsp::AudioBlock<double> b (lp2Buf); linXoverLP2.process (b); }
        { juce::dsp::AudioBlock<double> b (lp3Buf); linXoverLP3.process (b); }
        // Delay raw input
        int dlBufSize = inputDelayBuf.getNumSamples();
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* src = block.getChannelPointer (ch);
            auto* dly = inputDelayBuf.getWritePointer (ch);
            auto* delayed = bandBuffers[3].getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                int wIdx = (inputDelayWP + i) % dlBufSize;
                int rIdx = (wIdx - LP_DELAY + dlBufSize) % dlBufSize;
                delayed[i] = dly[rIdx];
                dly[wIdx] = src[i];
            }
        }
        inputDelayWP = (inputDelayWP + n) % dlBufSize;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* l1 = lp1Buf.getReadPointer (ch);
            auto* l2 = lp2Buf.getReadPointer (ch);
            auto* l3 = lp3Buf.getReadPointer (ch);
            auto* del = bandBuffers[3].getReadPointer (ch);
            auto* b0 = bandBuffers[0].getWritePointer (ch);
            auto* b1 = bandBuffers[1].getWritePointer (ch);
            auto* b2 = bandBuffers[2].getWritePointer (ch);
            auto* b3 = bandBuffers[3].getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                b0[i] = l1[i];
                b1[i] = l2[i] - l1[i];
                b2[i] = l3[i] - l2[i];
                b3[i] = del[i] - l3[i];
            }
        }
    }
    else
    {
        // IIR Linkwitz-Riley crossover
        xover1LP.setCutoffFrequency ((float) xoverFreq1.load());
        xover1HP.setCutoffFrequency ((float) xoverFreq1.load());
        xover2LP.setCutoffFrequency ((float) xoverFreq2.load());
        xover2HP.setCutoffFrequency ((float) xoverFreq2.load());
        xover3LP.setCutoffFrequency ((float) xoverFreq3.load());
        xover3HP.setCutoffFrequency ((float) xoverFreq3.load());
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::FloatVectorOperations::copy (bandBuffers[0].getWritePointer (ch), block.getChannelPointer (ch), n);
            juce::FloatVectorOperations::copy (tempBuffer.getWritePointer (ch), block.getChannelPointer (ch), n);
        }
        { juce::dsp::AudioBlock<double> b (bandBuffers[0]); juce::dsp::ProcessContextReplacing<double> c (b); xover1LP.process (c); }
        { juce::dsp::AudioBlock<double> b (tempBuffer);     juce::dsp::ProcessContextReplacing<double> c (b); xover1HP.process (c); }
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::FloatVectorOperations::copy (bandBuffers[1].getWritePointer (ch), tempBuffer.getReadPointer (ch), n);
            juce::FloatVectorOperations::copy (bandBuffers[2].getWritePointer (ch), tempBuffer.getReadPointer (ch), n);
        }
        { juce::dsp::AudioBlock<double> b (bandBuffers[1]); juce::dsp::ProcessContextReplacing<double> c (b); xover2LP.process (c); }
        { juce::dsp::AudioBlock<double> b (bandBuffers[2]); juce::dsp::ProcessContextReplacing<double> c (b); xover2HP.process (c); }
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy (bandBuffers[3].getWritePointer (ch), bandBuffers[2].getReadPointer (ch), n);
        { juce::dsp::AudioBlock<double> b (bandBuffers[2]); juce::dsp::ProcessContextReplacing<double> c (b); xover3LP.process (c); }
        { juce::dsp::AudioBlock<double> b (bandBuffers[3]); juce::dsp::ProcessContextReplacing<double> c (b); xover3HP.process (c); }
    }

    // ─── Process each band ───
    bool anySolo = false;
    for (int b = 0; b < NUM_BANDS; ++b) if (bandParams[b].solo.load()) anySolo = true;

    // Save phase-aligned dry: sum of all bands AFTER crossover (before dynamics)
    // This guarantees the dry has the same phase response (IIR allpass or linear phase delay)
    // as the wet signal — critical for clean global mix at any ratio
    double mixAmt = juce::jlimit (0.0, 1.0, globalMix.load() / 100.0);
    tempBuffer.setSize (2, n, false, false, true);
    tempBuffer.clear();
    if (mixAmt < 0.999)
    {
        for (int bnd = 0; bnd < NUM_BANDS; ++bnd)
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* src = bandBuffers[bnd].getReadPointer (ch);
                auto* dst = tempBuffer.getWritePointer (ch);
                for (int i = 0; i < n; ++i) dst[i] += src[i];
            }
    }

    block.clear();

    for (int bnd = 0; bnd < NUM_BANDS; ++bnd)
    {
        // Bypass: add dry band unprocessed
        if (bandParams[bnd].bypass.load())
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* src = bandBuffers[bnd].getReadPointer (ch);
                auto* dst = block.getChannelPointer (ch);
                for (int i = 0; i < n; ++i) dst[i] += src[i];
            }
            bandGRDisplay[(size_t) bnd].store (0.0f, std::memory_order_relaxed);
            continue;
        }

        // Solo: when ANY band is soloed, MUTE non-soloed bands
        if (anySolo && !bandParams[bnd].solo.load())
        {
            bandGRDisplay[(size_t) bnd].store (0.0f, std::memory_order_relaxed);
            continue;
        }

        double thresh = bandParams[bnd].threshold.load();
        double range  = bandParams[bnd].range.load();
        double ratio  = juce::jmax (1.0, (double) bandParams[bnd].ratio.load());
        double atkMs  = bandParams[bnd].attack.load();
        double relMs  = bandParams[bnd].release.load();
        double kneeDb = bandParams[bnd].knee.load();
        int    dynMode = bandParams[bnd].mode.load();
        double outG   = juce::Decibels::decibelsToGain ((double) bandParams[bnd].outputGain.load());

        // Envelope detector: fast peak follower (fixed ~5ms attack, ~50ms release)
        double envAtk = (currentSampleRate > 0) ? std::exp (-1.0 / (currentSampleRate * 0.005)) : 0;
        double envRel = (currentSampleRate > 0) ? std::exp (-1.0 / (currentSampleRate * 0.050)) : 0;

        // Gain smoothing: uses the user's attack/release for musical ballistics
        double gainAtkCoeff = (currentSampleRate > 0 && atkMs > 0.01) ? std::exp (-1.0 / (currentSampleRate * atkMs / 1000.0)) : 0;
        double gainRelCoeff = (currentSampleRate > 0 && relMs > 0.01) ? std::exp (-1.0 / (currentSampleRate * relMs / 1000.0)) : 0;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* bd = bandBuffers[bnd].getWritePointer (ch);
            auto& state = (ch == 0) ? bandStateL[(size_t) bnd] : bandStateR[(size_t) bnd];

            for (int i = 0; i < n; ++i)
            {
                double dry = bd[i];

                // Fast peak envelope detection (fixed time constants)
                double absIn = std::abs (dry);
                if (absIn > state.envelope)
                    state.envelope = envAtk * state.envelope + (1.0 - envAtk) * absIn;
                else
                    state.envelope = envRel * state.envelope + (1.0 - envRel) * absIn;

                // Compute target gain from gain computer
                double envDb = juce::Decibels::gainToDecibels (juce::jmax (state.envelope, 1e-10), -100.0);
                double targetGainDb = computeGainDb (envDb, thresh, ratio, kneeDb, range, dynMode);

                // Smooth gain in dB domain (more musical) using user attack/release
                double currentGainDb = juce::Decibels::gainToDecibels (juce::jmax (state.gainSmoothed, 1e-10), -100.0);
                double smoothCoeff = (targetGainDb < currentGainDb) ? gainAtkCoeff : gainRelCoeff;
                double smoothedGainDb = smoothCoeff * currentGainDb + (1.0 - smoothCoeff) * targetGainDb;
                state.gainSmoothed = juce::Decibels::decibelsToGain (smoothedGainDb);

                // Apply gain + output
                double processed = dry * state.gainSmoothed * outG;

                // Per-band mix (dry/wet)
                double bMix = juce::jlimit (0.0, 1.0, (double) bandParams[bnd].bandMix.load() / 100.0);
                bd[i] = dry * (1.0 - bMix) + processed * bMix;
            }
        }

        // Store GR for UI
        double grL = bandStateL[(size_t) bnd].gainSmoothed;
        double grR = bandStateR[(size_t) bnd].gainSmoothed;
        double avgGr = (grL + grR) * 0.5;
        bandGRDisplay[(size_t) bnd].store ((float) juce::Decibels::gainToDecibels (avgGr, -60.0), std::memory_order_relaxed);

        // Sum into output
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* src = bandBuffers[bnd].getReadPointer (ch);
            auto* dst = block.getChannelPointer (ch);
            for (int i = 0; i < n; ++i) dst[i] += src[i];
        }
    }

    // Global mix: blend processed sum with original dry input
    if (mixAmt < 0.999)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* wet = block.getChannelPointer (ch);
            auto* dry = tempBuffer.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                wet[i] = dry[i] * (1.0 - mixAmt) + wet[i] * mixAmt;
        }
    }

    // M/S decode
    if (ms > 0)
    {
        auto* ch0 = block.getChannelPointer (0);
        auto* ch1 = block.getChannelPointer (1);
        for (int i = 0; i < n; ++i)
        {
            double m = ch0[i], s = ch1[i];
            ch0[i] = m + s;
            ch1[i] = m - s;
        }
    }
    updateOutputMeters (block);
}

double MultibandDynamicsStage::computeGainDb (double inputDb, double threshDb, double ratioVal,
                                               double kneeDb, double rangeDb, int dynMode) const
{
    // dynMode: 0=Compress, 1=Expand
    // rangeDb: negative=downward, positive=upward
    // Returns gain in dB to apply to the signal

    double gainDb = 0.0;
    double halfKnee = kneeDb * 0.5;

    if (dynMode == 0)
    {
        // COMPRESS
        if (rangeDb <= 0)
        {
            // Downward compression: above threshold, reduce gain
            double overDb = inputDb - threshDb;
            if (overDb <= -halfKnee)
                gainDb = 0.0;
            else if (overDb < halfKnee && kneeDb > 0.01)
            {
                double x = overDb + halfKnee;
                gainDb = -(x * x / (2.0 * kneeDb)) * (1.0 - 1.0 / ratioVal);
            }
            else
                gainDb = -(overDb) * (1.0 - 1.0 / ratioVal);

            gainDb = juce::jmax (gainDb, rangeDb);
        }
        else
        {
            // Upward compression: below threshold, boost
            double underDb = threshDb - inputDb;
            if (underDb <= -halfKnee)
                gainDb = 0.0;
            else if (underDb < halfKnee && kneeDb > 0.01)
            {
                double x = underDb + halfKnee;
                gainDb = (x * x / (2.0 * kneeDb)) * (1.0 - 1.0 / ratioVal);
            }
            else
                gainDb = underDb * (1.0 - 1.0 / ratioVal);

            gainDb = juce::jmin (gainDb, rangeDb);
        }
    }
    else
    {
        // EXPAND
        if (rangeDb <= 0)
        {
            // Downward expansion (gate): below threshold, reduce gain
            double underDb = threshDb - inputDb;
            if (underDb <= -halfKnee)
                gainDb = 0.0;
            else if (underDb < halfKnee && kneeDb > 0.01)
            {
                double x = underDb + halfKnee;
                gainDb = -(x * x / (2.0 * kneeDb)) * (ratioVal - 1.0);
            }
            else
                gainDb = -underDb * (ratioVal - 1.0);

            gainDb = juce::jmax (gainDb, rangeDb);
        }
        else
        {
            // Upward expansion: above threshold, boost
            double overDb = inputDb - threshDb;
            if (overDb <= -halfKnee)
                gainDb = 0.0;
            else if (overDb < halfKnee && kneeDb > 0.01)
            {
                double x = overDb + halfKnee;
                gainDb = (x * x / (2.0 * kneeDb)) * (ratioVal - 1.0);
            }
            else
                gainDb = overDb * (ratioVal - 1.0);

            gainDb = juce::jmin (gainDb, rangeDb);
        }
    }

    return gainDb;
}

void MultibandDynamicsStage::reset()
{
    xover1LP.reset(); xover1HP.reset(); xover2LP.reset(); xover2HP.reset();
    xover3LP.reset(); xover3HP.reset();
    linXoverLP1.reset(); linXoverLP2.reset(); linXoverLP3.reset();
    linXoverBuilt = false;
    inputDelayBuf.clear(); inputDelayWP = 0;
    for (auto& s : bandStateL) { s.envelope = 0; s.gainSmoothed = 1.0; }
    for (auto& s : bandStateR) { s.envelope = 0; s.gainSmoothed = 1.0; }
}

int MultibandDynamicsStage::getLatencySamples() const
{
    if (!stageOn.load()) return 0;
    if (xoverMode.load() == 1 && linXoverBuilt) return LP_DELAY;
    return 0;
}

void MultibandDynamicsStage::rebuildLinearPhaseCrossover()
{
    double sr = currentSampleRate;
    if (sr <= 0) return;
    linXoverLP1.designLowpass ((double) xoverFreq1.load(), sr, 24);
    linXoverLP2.designLowpass ((double) xoverFreq2.load(), sr, 24);
    linXoverLP3.designLowpass ((double) xoverFreq3.load(), sr, 24);
    linXoverBuilt = true;
}

void MultibandDynamicsStage::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add (std::make_unique<juce::AudioParameterBool>   ("S8_MBDyn_On",    "MB Dyn On",   false));
    layout.add (std::make_unique<juce::AudioParameterChoice> ("S8_MBDyn_MS",    "Channel",     juce::StringArray {"Stereo","Mid","Side"}, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> ("S8_MBDyn_XMode", "XMode",       juce::StringArray {"Min Phase","Linear Phase"}, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat>  ("S8_MBDyn_Xover1","Xover1",      juce::NormalisableRange<float>(20,500,1,0.4f), 120));
    layout.add (std::make_unique<juce::AudioParameterFloat>  ("S8_MBDyn_Xover2","Xover2",      juce::NormalisableRange<float>(200,5000,1,0.35f), 1000));
    layout.add (std::make_unique<juce::AudioParameterFloat>  ("S8_MBDyn_Xover3","Xover3",      juce::NormalisableRange<float>(1000,16000,1,0.3f), 5000));
    layout.add (std::make_unique<juce::AudioParameterFloat>  ("S8_MBDyn_Mix",   "Mix",         juce::NormalisableRange<float>(0,100,1), 100));

    for (int b = 1; b <= 4; ++b)
    {
        auto p = "S8_MBDyn_B" + juce::String (b) + "_";
        auto l = "B" + juce::String (b) + " ";
        layout.add (std::make_unique<juce::AudioParameterChoice> (p + "Mode",    l + "Mode",     juce::StringArray {"Compress","Expand"}, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat>  (p + "Thresh",  l + "Thresh",   juce::NormalisableRange<float>(-60,0,0.1f), 0));
        layout.add (std::make_unique<juce::AudioParameterFloat>  (p + "Range",   l + "Range",    juce::NormalisableRange<float>(-48,48,0.1f), 0));
        layout.add (std::make_unique<juce::AudioParameterFloat>  (p + "Ratio",   l + "Ratio",    juce::NormalisableRange<float>(1,20,0.1f,0.5f), 2));
        layout.add (std::make_unique<juce::AudioParameterFloat>  (p + "Attack",  l + "Attack",   juce::NormalisableRange<float>(0.1f,500,0.1f,0.3f), 30));
        layout.add (std::make_unique<juce::AudioParameterFloat>  (p + "Release", l + "Release",  juce::NormalisableRange<float>(10,2000,1,0.3f), 200));
        layout.add (std::make_unique<juce::AudioParameterFloat>  (p + "Knee",    l + "Knee",     juce::NormalisableRange<float>(0,36,0.1f), 12));
        layout.add (std::make_unique<juce::AudioParameterFloat>  (p + "Output",  l + "Output",   juce::NormalisableRange<float>(-12,12,0.1f), 0));
        layout.add (std::make_unique<juce::AudioParameterBool>   (p + "Solo",    l + "Solo",     false));
        layout.add (std::make_unique<juce::AudioParameterBool>   (p + "Bypass",  l + "Bypass",   false));
        layout.add (std::make_unique<juce::AudioParameterFloat>  (p + "BandMix", l + "BandMix",  juce::NormalisableRange<float>(0,100,1), 100));
    }
}

void MultibandDynamicsStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store (a.getRawParameterValue ("S8_MBDyn_On")->load() > 0.5f);
    msMode.store ((int) a.getRawParameterValue ("S8_MBDyn_MS")->load());
    xoverMode.store ((int) a.getRawParameterValue ("S8_MBDyn_XMode")->load());
    xoverFreq1.store (a.getRawParameterValue ("S8_MBDyn_Xover1")->load());
    xoverFreq2.store (a.getRawParameterValue ("S8_MBDyn_Xover2")->load());
    xoverFreq3.store (a.getRawParameterValue ("S8_MBDyn_Xover3")->load());
    globalMix.store (a.getRawParameterValue ("S8_MBDyn_Mix")->load());

    for (int b = 0; b < 4; ++b)
    {
        auto p = "S8_MBDyn_B" + juce::String (b + 1) + "_";
        bandParams[b].mode.store ((int) a.getRawParameterValue (p + "Mode")->load());
        bandParams[b].threshold.store (a.getRawParameterValue (p + "Thresh")->load());
        bandParams[b].range.store (a.getRawParameterValue (p + "Range")->load());
        bandParams[b].ratio.store (a.getRawParameterValue (p + "Ratio")->load());
        bandParams[b].attack.store (a.getRawParameterValue (p + "Attack")->load());
        bandParams[b].release.store (a.getRawParameterValue (p + "Release")->load());
        bandParams[b].knee.store (a.getRawParameterValue (p + "Knee")->load());
        bandParams[b].outputGain.store (a.getRawParameterValue (p + "Output")->load());
        bandParams[b].solo.store (a.getRawParameterValue (p + "Solo")->load() > 0.5f);
        bandParams[b].bypass.store (a.getRawParameterValue (p + "Bypass")->load() > 0.5f);
        bandParams[b].bandMix.store (a.getRawParameterValue (p + "BandMix")->load());
    }

    // Rebuild linear phase if needed
    if (xoverMode.load() == 1)
    {
        float xf1 = xoverFreq1.load(), xf2 = xoverFreq2.load(), xf3 = xoverFreq3.load();
        bool needRebuild = !linXoverBuilt
            || std::abs (xf1 - lastXF1) > 10.0f
            || std::abs (xf2 - lastXF2) > 10.0f
            || std::abs (xf3 - lastXF3) > 10.0f;
        if (needRebuild)
        {
            rebuildLinearPhaseCrossover();
            lastXF1 = xf1; lastXF2 = xf2; lastXF3 = xf3;
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  LIMITER STAGE
// ─────────────────────────────────────────────────────────────

void LimiterStage::prepare(double sr, int bs)
{
    currentSampleRate = sr; currentBlockSize = bs;
    int maxDel = (int)(sr * 0.006) + 1; // 6ms max lookahead
    delayBuffer.setSize(2, maxDel + bs); delayBuffer.clear(); delayWritePos = 0;
    lookaheadSamples = juce::jmax(1, (int)(sr * lookaheadMs.load() / 1000.0));
    grEnvelope = 1.0; grEnvelope2 = 1.0; grSlow = 1.0; busSmooth = 1.0;
    rmsEnvelope = 0.0;
    attackCoeff = std::exp(-1.0 / (sr * 0.0001));
    releaseCoeff = std::exp(-1.0 / (sr * releaseMs.load() / 1000.0));
    for (int i = 0; i < 4; ++i) { tpHistL[i] = 0; tpHistR[i] = 0; }
    warmPrevL = 0; warmPrevR = 0;
}

// ─── Hermite cubic interpolation for True Peak 4x ───
double LimiterStage::hermiteInterp(double t, double p0, double p1, double p2, double p3)
{
    // Cubic Hermite (Catmull-Rom) — standard for inter-sample peak detection
    double a = -0.5 * p0 + 1.5 * p1 - 1.5 * p2 + 0.5 * p3;
    double b =        p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
    double c = -0.5 * p0             + 0.5 * p2;
    double d =                   p1;
    return ((a * t + b) * t + c) * t + d;
}

// ─── Peak detection: sample-accurate or True Peak 4x ───
double LimiterStage::detectPeak(double sampleL, double sampleR, bool useTruePeak)
{
    double peak = std::max(std::abs(sampleL), std::abs(sampleR));

    if (useTruePeak)
    {
        // Shift history buffers
        tpHistL[0] = tpHistL[1]; tpHistL[1] = tpHistL[2]; tpHistL[2] = tpHistL[3]; tpHistL[3] = sampleL;
        tpHistR[0] = tpHistR[1]; tpHistR[1] = tpHistR[2]; tpHistR[2] = tpHistR[3]; tpHistR[3] = sampleR;

        // Check 4x oversampled points between sample [2] and [3] (current pair)
        // t = 0.25, 0.5, 0.75 — catches inter-sample peaks
        for (int k = 1; k <= 3; ++k)
        {
            double t = k / 4.0;
            double interpL = hermiteInterp(t, tpHistL[0], tpHistL[1], tpHistL[2], tpHistL[3]);
            double interpR = hermiteInterp(t, tpHistR[0], tpHistR[1], tpHistR[2], tpHistR[3]);
            peak = std::max(peak, std::max(std::abs(interpL), std::abs(interpR)));
        }
    }
    return peak;
}

// ─── Hard brick-wall gain computation ───
double LimiterStage::computeGain(double peakLevel, double ceiling)
{
    return (peakLevel <= ceiling || peakLevel < 1e-10) ? 1.0 : ceiling / peakLevel;
}

// ─── Soft knee gain computation (for Bus style) ───
double LimiterStage::computeGainSoftKnee(double peakLevel, double ceiling, double kneeDb)
{
    if (peakLevel < 1e-10) return 1.0;
    double peakDb = juce::Decibels::gainToDecibels(peakLevel, -100.0);
    double ceilDb = juce::Decibels::gainToDecibels(ceiling, -100.0);
    double halfKnee = kneeDb * 0.5;

    if (peakDb <= ceilDb - halfKnee)
        return 1.0; // Below knee — no limiting
    else if (peakDb >= ceilDb + halfKnee)
        return ceiling / peakLevel; // Above knee — full limiting
    else
    {
        // In the knee region — smooth quadratic transition
        double x = peakDb - ceilDb + halfKnee;
        double grDb = -(x * x) / (2.0 * kneeDb);
        return juce::Decibels::decibelsToGain(grDb);
    }
}

void LimiterStage::process(juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters(block);
    int n = (int)block.getNumSamples(), nch = (int)block.getNumChannels();
    int delSz = delayBuffer.getNumSamples();
    double inG = juce::Decibels::decibelsToGain((double)inputGain.load());
    double ceil = juce::Decibels::decibelsToGain((double)ceilingDb.load());
    int limSt = style.load();
    bool useTP = truePeakOn.load();
    double sr = currentSampleRate;

    // Apply input gain
    if (std::abs(inG - 1.0) > 1e-6) block.multiplyBy(inG);

    // ─── WARM style: subtle 2nd harmonic saturation BEFORE limiting ───
    if (limSt == 2)
    {
        double driveAmt = 0.15; // Very subtle — just adds warmth
        for (int i = 0; i < n; ++i)
        {
            if (nch >= 1)
            {
                double s = block.getSample(0, i);
                double driven = s + driveAmt * s * s * (s > 0 ? 1.0 : -1.0);
                // Soft clip the drive to prevent level increase
                driven = std::tanh(driven * 0.95) / std::tanh(0.95);
                block.setSample(0, i, driven);
            }
            if (nch >= 2)
            {
                double s = block.getSample(1, i);
                double driven = s + driveAmt * s * s * (s > 0 ? 1.0 : -1.0);
                driven = std::tanh(driven * 0.95) / std::tanh(0.95);
                block.setSample(1, i, driven);
            }
        }
    }

    // ─── Cache release coefficient with style modifiers ───
    double baseRelCoeff = releaseCoeff;

    for (int i = 0; i < n; ++i)
    {
        // Write to delay buffer
        for (int ch = 0; ch < nch && ch < 2; ++ch)
            delayBuffer.setSample(ch, (delayWritePos + i) % delSz, block.getSample(ch, i));

        // Peak detection
        double sL = (nch >= 1) ? block.getSample(0, i) : 0;
        double sR = (nch >= 2) ? block.getSample(1, i) : sL;
        double peak = detectPeak(sL, sR, useTP);

        // ─── Compute target gain based on style ───
        double tg;

        switch (limSt)
        {
            case 5: // Bus — soft knee (6 dB)
                tg = computeGainSoftKnee(peak, ceil, 6.0);
                break;
            case 4: // Modern — first stage: ceiling + 3dB
            {
                double ceilStage1 = ceil * 1.4125; // +3dB
                tg = computeGain(peak, ceilStage1);
                break;
            }
            default: // All others: hard brick-wall
                tg = computeGain(peak, ceil);
                break;
        }

        // ─── Attack: style-dependent envelope following ───
        if (tg < grEnvelope)
        {
            double attackMs;
            switch (limSt)
            {
                case 1:  attackMs = 0.01;  break; // Aggressive: 10µs — ultra-fast
                case 2:  attackMs = 0.15;  break; // Warm: 150µs — lets initial transient color
                case 3:  attackMs = 0.08;  break; // Punch: 80µs — fast but not instant
                case 4:  attackMs = 0.05;  break; // Modern: 50µs — clean
                case 5:  attackMs = 0.2;   break; // Bus: 200µs — musical
                default: attackMs = 0.05;  break; // Transparent: 50µs
            }
            double ac = std::exp(-1.0 / (sr * attackMs / 1000.0));
            grEnvelope = ac * grEnvelope + (1.0 - ac) * tg;
        }
        else
        {
            // ─── Release: style-dependent ───
            double rc = baseRelCoeff;

            if (autoRelease.load())
            {
                // Program-dependent release: more GR → longer release (less pumping)
                double grDb = juce::Decibels::gainToDecibels(grEnvelope, -100.0);
                double ms;
                switch (limSt)
                {
                    case 1:  ms = juce::jlimit(15.0, 200.0, juce::jmap(grDb, -12.0, 0.0, 120.0, 15.0)); break; // Aggressive: short
                    case 2:  ms = juce::jlimit(50.0, 800.0, juce::jmap(grDb, -12.0, 0.0, 500.0, 80.0)); break; // Warm: long
                    case 3:  ms = juce::jlimit(30.0, 400.0, juce::jmap(grDb, -12.0, 0.0, 300.0, 40.0)); break; // Punch
                    case 5:  ms = juce::jlimit(40.0, 600.0, juce::jmap(grDb, -12.0, 0.0, 400.0, 60.0)); break; // Bus: musical
                    default: ms = juce::jlimit(20.0, 500.0, juce::jmap(grDb, -12.0, 0.0, 300.0, 50.0)); break; // Transparent/Modern
                }
                rc = std::exp(-1.0 / (sr * ms / 1000.0));
            }
            else
            {
                // Manual release with style modifier
                switch (limSt)
                {
                    case 1:  rc = std::pow(rc, 1.4);  break; // Aggressive: faster
                    case 2:  rc = std::pow(rc, 0.6);  break; // Warm: slower
                    case 5:  rc = std::pow(rc, 0.7);  break; // Bus: slower
                    default: break;
                }
            }
            grEnvelope = rc * grEnvelope + (1.0 - rc) * 1.0;
        }

        // ─── PUNCH style: dual envelope — preserve transients ───
        double finalGR = grEnvelope;
        if (limSt == 3)
        {
            // RMS envelope for sustained level detection
            double rmsCoeff = std::exp(-1.0 / (sr * 0.01)); // 10ms RMS window
            rmsEnvelope = rmsCoeff * rmsEnvelope + (1.0 - rmsCoeff) * (sL * sL + sR * sR) * 0.5;
            double rmsPeak = std::sqrt(rmsEnvelope) * 1.414; // Convert RMS to peak estimate
            double tgSlow = computeGain(rmsPeak, ceil);

            // Slow envelope for sustained limiting
            double slowAttCoeff = std::exp(-1.0 / (sr * 0.002)); // 2ms
            double slowRelCoeff = std::exp(-1.0 / (sr * 0.15));  // 150ms
            if (tgSlow < grSlow)
                grSlow = slowAttCoeff * grSlow + (1.0 - slowAttCoeff) * tgSlow;
            else
                grSlow = slowRelCoeff * grSlow + (1.0 - slowRelCoeff) * 1.0;

            // Final GR: blend peak and slow — fast peaks get through, sustained is limited
            // Use the LESS aggressive of the two (allows transients)
            double transientBoost = 0.7; // How much transient to preserve (0=none, 1=full)
            finalGR = grEnvelope * (1.0 - transientBoost) + std::max(grEnvelope, grSlow) * transientBoost;
            // But never exceed ceiling
            finalGR = juce::jmin(finalGR, computeGain(peak * 1.05, ceil)); // Safety margin
        }

        // ─── MODERN style: second stage limiting ───
        if (limSt == 4)
        {
            // Read from delay with first stage GR applied, check against final ceiling
            int rIdx = (delayWritePos + i - lookaheadSamples + delSz) % delSz;
            double stage1L = delayBuffer.getSample(0, rIdx) * grEnvelope;
            double stage1R = (nch >= 2) ? delayBuffer.getSample(1, rIdx) * grEnvelope : stage1L;
            double stage1Peak = std::max(std::abs(stage1L), std::abs(stage1R));

            double tg2 = computeGain(stage1Peak, ceil);
            double ac2 = std::exp(-1.0 / (sr * 0.02 / 1000.0)); // 20µs attack
            if (tg2 < grEnvelope2)
                grEnvelope2 = ac2 * grEnvelope2 + (1.0 - ac2) * tg2;
            else
            {
                double rc2 = std::exp(-1.0 / (sr * 0.05)); // 50ms release
                grEnvelope2 = rc2 * grEnvelope2 + (1.0 - rc2) * 1.0;
            }
            finalGR = grEnvelope * grEnvelope2; // Combined two-stage GR
        }

        // ─── BUS style: smooth the GR for musical character ───
        if (limSt == 5)
        {
            double busCoeff = std::exp(-1.0 / (sr * 0.003)); // 3ms smoothing
            busSmooth = busCoeff * busSmooth + (1.0 - busCoeff) * finalGR;
            finalGR = busSmooth;
        }

        // ─── Read from delay and apply gain reduction ───
        int rIdx = (delayWritePos + i - lookaheadSamples + delSz) % delSz;
        for (int ch = 0; ch < nch && ch < 2; ++ch)
        {
            double sample = delayBuffer.getSample(ch, rIdx) * finalGR;
            // HARD CLIP SAFETY — absolute ceiling guarantee
            sample = juce::jlimit(-ceil, ceil, sample);
            block.setSample(ch, i, sample);
        }

        meterData.gainReduction.store((float)juce::Decibels::gainToDecibels(finalGR, -100.0));

        // Push to GR history (subsampled for display)
        if (++grHistSubsample >= 64)
        {
            grHistSubsample = 0;
            int pos = grHistoryPos.load (std::memory_order_relaxed);
            grHistory[(size_t)pos] = (float)juce::Decibels::gainToDecibels(finalGR, -100.0);
            grHistoryPos.store ((pos + 1) % GR_HISTORY_SIZE, std::memory_order_relaxed);
        }
    }
    delayWritePos = (delayWritePos + n) % delSz;
    updateOutputMeters(block);
}

void LimiterStage::reset()
{
    delayBuffer.clear(); delayWritePos = 0;
    grEnvelope = 1.0; grEnvelope2 = 1.0; grSlow = 1.0; busSmooth = 1.0;
    rmsEnvelope = 0.0; grHistSubsample = 0;
    grHistory.fill (0); grHistoryPos.store (0);
    for (int i = 0; i < 4; ++i) { tpHistL[i] = 0; tpHistR[i] = 0; }
    warmPrevL = 0; warmPrevR = 0;
}

int LimiterStage::getLatencySamples() const { return stageOn.load() ? lookaheadSamples : 0; }

void LimiterStage::addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S7_Lim_On", "Limiter On", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S7_Lim_Input", "Lim Input",
        juce::NormalisableRange<float>(0, 24, 0.1f), 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S7_Lim_Ceiling", "Lim Ceiling",
        juce::NormalisableRange<float>(-3, 0, 0.1f), -0.3f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S7_Lim_Release", "Lim Release",
        juce::NormalisableRange<float>(10, 500, 1, 0.5f), 100));
    layout.add(std::make_unique<juce::AudioParameterBool>("S7_Lim_AutoRelease", "Auto Rel", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S7_Lim_Lookahead", "Lookahead",
        juce::NormalisableRange<float>(0.1f, 5, 0.1f), 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S7_Lim_Style", "Lim Style",
        juce::StringArray{"Transparent", "Aggressive", "Warm", "Punch", "Modern", "Bus"}, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>("S7_Lim_TruePeak", "True Peak", true));
}

void LimiterStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S7_Lim_On")->load() > 0.5f);
    inputGain.store(a.getRawParameterValue("S7_Lim_Input")->load());
    ceilingDb.store(a.getRawParameterValue("S7_Lim_Ceiling")->load());
    releaseMs.store(a.getRawParameterValue("S7_Lim_Release")->load());
    autoRelease.store(a.getRawParameterValue("S7_Lim_AutoRelease")->load() > 0.5f);
    style.store((int)a.getRawParameterValue("S7_Lim_Style")->load());
    truePeakOn.store(a.getRawParameterValue("S7_Lim_TruePeak")->load() > 0.5f);
    float nl = a.getRawParameterValue("S7_Lim_Lookahead")->load();
    if (std::abs(nl - lookaheadMs.load()) > 0.01f)
    {
        lookaheadMs.store(nl);
        lookaheadSamples = juce::jmax(1, (int)(currentSampleRate * nl / 1000.0));
    }
    if (currentSampleRate > 0)
        releaseCoeff = std::exp(-1.0 / (currentSampleRate * releaseMs.load() / 1000.0));
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

void OutputMeter::prepare (double sr, int bs)
{
    sampleRate = sr; blockSize = bs;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 1 };
    preFilterL.prepare (spec); preFilterR.prepare (spec);
    rlbFilterL.prepare (spec); rlbFilterR.prepare (spec);
    auto pre = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 1681, 0.7, juce::Decibels::decibelsToGain (4.0f));
    *preFilterL.coefficients = *pre; *preFilterR.coefficients = *pre;
    auto rlb = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 38, 0.5);
    *rlbFilterL.coefficients = *rlb; *rlbFilterR.coefficients = *rlb;

    blocksFor400ms = std::max (1, (int) (sr * 0.4 / bs));
    blocksFor3s    = std::max (1, (int) (sr * 3.0 / bs));
    peakHoldSamples = (int) (sr * 2.0 / bs);

    momentaryPower = 0; momentaryBlockCount = 0;
    stWriteIdx = 0; stBlockCount = 0;
    intWriteIdx = 0; intBlockCount = 0;
    peakHold = -100.0f; peakHoldCounter = 0;
    fifoIndex = 0;

    // Imager crossover filters (stereo = 2 channels)
    // Set defaults if not already configured
    if (imagerXover[0].load() < 10.0f)
    {
        imagerXover[0].store (120.0f);
        imagerXover[1].store (1000.0f);
        imagerXover[2].store (8000.0f);
    }
    juce::dsp::ProcessSpec spec2 { sr, (juce::uint32) bs, 2 };
    imgXover1LP.prepare (spec2); imgXover1HP.prepare (spec2);
    imgXover2LP.prepare (spec2); imgXover2HP.prepare (spec2);
    imgXover3LP.prepare (spec2); imgXover3HP.prepare (spec2);
    imgXover1LP.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    imgXover1HP.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    imgXover2LP.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    imgXover2HP.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    imgXover3LP.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    imgXover3HP.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    for (auto& b : imgBandBufs) b.setSize (2, bs);
    imgTempBuf.setSize (2, bs);
    for (int b = 0; b < NUM_IMG_BANDS; ++b)
        if (bandWidthValues[b].load() < 1.0f) bandWidthValues[b].store (100.0f);
}

void OutputMeter::process (juce::AudioBuffer<float>& buf)
{
    int n = buf.getNumSamples();
    if (buf.getNumChannels() < 2 || n == 0) return;

    auto* rL = buf.getReadPointer (0);
    auto* rR = buf.getReadPointer (1);

    // ─── True Peak with hold + decay ───
    float pL = 0, pR = 0;
    for (int i = 0; i < n; ++i)
    {
        pL = std::max (pL, std::abs (rL[i]));
        pR = std::max (pR, std::abs (rR[i]));
    }
    float pDb = juce::Decibels::gainToDecibels (std::max (pL, pR), -100.f);

    if (pDb >= peakHold)
    {
        peakHold = pDb;
        peakHoldCounter = 0;
    }
    else
    {
        peakHoldCounter++;
        if (peakHoldCounter > peakHoldSamples)
            peakHold = peakHold * 0.97f + pDb * 0.03f; // smooth decay
    }
    truePeak.store (peakHold);

    // ─── K-weighted power for loudness ───
    double kSum = 0;
    for (int i = 0; i < n; ++i)
    {
        float kL = rlbFilterL.processSample (preFilterL.processSample (rL[i]));
        float kR = rlbFilterR.processSample (preFilterR.processSample (rR[i]));
        kSum += (double)(kL * kL + kR * kR);
    }
    double blockPower = kSum / (double) n;

    // ─── Momentary LUFS (400ms) ───
    momentaryPower += blockPower;
    momentaryBlockCount++;
    if (momentaryBlockCount >= blocksFor400ms)
    {
        double avg = momentaryPower / momentaryBlockCount;
        momentaryLUFS.store ((float)(-0.691 + 10 * std::log10 (std::max (avg, 1e-10))));
        momentaryPower = 0;
        momentaryBlockCount = 0;
    }

    // ─── Short-term LUFS (3s sliding window) ───
    stPowerRing[(size_t)(stWriteIdx % MAX_ST_BLOCKS)] = blockPower;
    stWriteIdx++;
    stBlockCount = std::min (stBlockCount + 1, blocksFor3s);
    {
        int count = std::min (stBlockCount, MAX_ST_BLOCKS);
        double sum = 0;
        for (int i = 0; i < count; ++i)
        {
            int idx = (stWriteIdx - 1 - i + MAX_ST_BLOCKS * 100) % MAX_ST_BLOCKS;
            sum += stPowerRing[(size_t) idx];
        }
        if (count > 0)
        {
            double avg = sum / count;
            shortTermLUFS.store ((float)(-0.691 + 10 * std::log10 (std::max (avg, 1e-10))));
        }
    }

    // ─── Integrated LUFS (gated, ring buffer bounded) ───
    intPowerRing[(size_t)(intWriteIdx % MAX_INT_BLOCKS)] = blockPower;
    intWriteIdx++;
    intBlockCount = std::min (intBlockCount + 1, MAX_INT_BLOCKS);
    {
        int count = intBlockCount;
        // First pass: absolute gate at -70 LUFS
        double s1 = 0; int c1 = 0;
        for (int i = 0; i < count; ++i)
        {
            int idx = (intWriteIdx - 1 - i + MAX_INT_BLOCKS * 100) % MAX_INT_BLOCKS;
            double p = intPowerRing[(size_t) idx];
            double l = -0.691 + 10 * std::log10 (std::max (p, 1e-10));
            if (l > -70) { s1 += p; ++c1; }
        }
        if (c1 > 0)
        {
            double rg = -0.691 + 10 * std::log10 (s1 / c1) - 10; // relative gate
            double s2 = 0; int c2 = 0;
            for (int i = 0; i < count; ++i)
            {
                int idx = (intWriteIdx - 1 - i + MAX_INT_BLOCKS * 100) % MAX_INT_BLOCKS;
                double p = intPowerRing[(size_t) idx];
                double l = -0.691 + 10 * std::log10 (std::max (p, 1e-10));
                if (l > rg) { s2 += p; ++c2; }
            }
            if (c2 > 0)
                integratedLUFS.store ((float)(-0.691 + 10 * std::log10 (s2 / c2)));
        }
    }

    // ─── Stereo metering ───
    double sumLR = 0, sumLL = 0, sumRR = 0;
    double sumLpow = 0, sumRpow = 0;
    double sumMid = 0, sumSide = 0;
    for (int i = 0; i < n; ++i)
    {
        double l = (double) rL[i], r = (double) rR[i];
        sumLR += l * r;
        sumLL += l * l;
        sumRR += r * r;
        sumLpow += l * l;
        sumRpow += r * r;
        double mid = (l + r) * 0.5;
        double side = (l - r) * 0.5;
        sumMid += mid * mid;
        sumSide += side * side;
    }

    // Correlation: -1 to +1 (Pearson)
    double denom = std::sqrt (sumLL * sumRR);
    float corr = (denom > 1e-10) ? (float)(sumLR / denom) : 1.0f;
    correlation.store (corr);

    // Balance: -1 (left) to +1 (right)
    float lPow = (float) std::sqrt (sumLpow / n);
    float rPow = (float) std::sqrt (sumRpow / n);
    float total = lPow + rPow;
    float bal = (total > 1e-6f) ? (rPow - lPow) / total : 0.0f;
    balance.store (bal);

    // L/R RMS in dB
    lRms.store (juce::Decibels::gainToDecibels (lPow, -100.f));
    rRms.store (juce::Decibels::gainToDecibels (rPow, -100.f));

    // Stereo Width: ratio of side to mid energy
    float midPow = (float) std::sqrt (sumMid / n);
    float sidePow = (float) std::sqrt (sumSide / n);
    float width = (midPow > 1e-6f) ? sidePow / (midPow + sidePow) : 0.0f;
    stereoWidth.store (width);

    // ─── Multiband Imager analysis (4-band split) ───
    {
        for (auto& bb : imgBandBufs) bb.setSize (2, n, false, false, true);
        imgTempBuf.setSize (2, n, false, false, true);

        // Update crossover frequencies
        imgXover1LP.setCutoffFrequency (imagerXover[0].load());
        imgXover1HP.setCutoffFrequency (imagerXover[0].load());
        imgXover2LP.setCutoffFrequency (imagerXover[1].load());
        imgXover2HP.setCutoffFrequency (imagerXover[1].load());
        imgXover3LP.setCutoffFrequency (imagerXover[2].load());
        imgXover3HP.setCutoffFrequency (imagerXover[2].load());

        // Copy input to band0 and temp
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::FloatVectorOperations::copy (imgBandBufs[0].getWritePointer (ch), buf.getReadPointer (ch), n);
            juce::FloatVectorOperations::copy (imgTempBuf.getWritePointer (ch), buf.getReadPointer (ch), n);
        }

        // Split at xover1 → band0 = low, temp = above
        { juce::dsp::AudioBlock<float> b (imgBandBufs[0]); juce::dsp::ProcessContextReplacing<float> c (b); imgXover1LP.process (c); }
        { juce::dsp::AudioBlock<float> b (imgTempBuf); juce::dsp::ProcessContextReplacing<float> c (b); imgXover1HP.process (c); }

        // Split temp at xover2 → band1 = lo-mid, band2+temp = above
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::FloatVectorOperations::copy (imgBandBufs[1].getWritePointer (ch), imgTempBuf.getReadPointer (ch), n);
            juce::FloatVectorOperations::copy (imgBandBufs[2].getWritePointer (ch), imgTempBuf.getReadPointer (ch), n);
        }
        { juce::dsp::AudioBlock<float> b (imgBandBufs[1]); juce::dsp::ProcessContextReplacing<float> c (b); imgXover2LP.process (c); }
        { juce::dsp::AudioBlock<float> b (imgBandBufs[2]); juce::dsp::ProcessContextReplacing<float> c (b); imgXover2HP.process (c); }

        // Split band2 at xover3 → band2 = hi-mid, band3 = high
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy (imgBandBufs[3].getWritePointer (ch), imgBandBufs[2].getReadPointer (ch), n);
        { juce::dsp::AudioBlock<float> b (imgBandBufs[2]); juce::dsp::ProcessContextReplacing<float> c (b); imgXover3LP.process (c); }
        { juce::dsp::AudioBlock<float> b (imgBandBufs[3]); juce::dsp::ProcessContextReplacing<float> c (b); imgXover3HP.process (c); }

        // Measure per-band stereo
        for (int bnd = 0; bnd < NUM_IMG_BANDS; ++bnd)
        {
            auto* bL = imgBandBufs[bnd].getReadPointer (0);
            auto* bR = imgBandBufs[bnd].getReadPointer (1);

            double sLR = 0, sLL = 0, sRR = 0;
            double sLpow = 0, sRpow = 0;
            double sMid = 0, sSide = 0;

            for (int i = 0; i < n; ++i)
            {
                double l = (double) bL[i], r = (double) bR[i];
                sLR += l * r; sLL += l * l; sRR += r * r;
                sLpow += l * l; sRpow += r * r;
                double mid = (l + r) * 0.5;
                double side = (l - r) * 0.5;
                sMid += mid * mid; sSide += side * side;
            }

            double den = std::sqrt (sLL * sRR);
            float bCorr = (den > 1e-10) ? (float)(sLR / den) : 1.0f;
            bandStereo[bnd].correlation.store (bCorr);

            float bLpow = (float) std::sqrt (sLpow / n);
            float bRpow = (float) std::sqrt (sRpow / n);
            float bTotal = bLpow + bRpow;
            bandStereo[bnd].balance.store ((bTotal > 1e-6f) ? (bRpow - bLpow) / bTotal : 0.0f);
            bandStereo[bnd].lRms.store (juce::Decibels::gainToDecibels (bLpow, -100.f));
            bandStereo[bnd].rRms.store (juce::Decibels::gainToDecibels (bRpow, -100.f));

            float bMidPow = (float) std::sqrt (sMid / n);
            float bSidePow = (float) std::sqrt (sSide / n);
            float bWidth = (bMidPow > 1e-6f) ? bSidePow / (bMidPow + bSidePow) : 0.0f;
            bandStereo[bnd].width.store (bWidth);
        }

        // ─── Width is now applied BEFORE limiter in ProcessingEngine::process ───
        // OutputMeter only does analysis, no audio modification
        }

    // ─── FFT FIFO (mono + L/R for Mid/Side) ───
    for (int i = 0; i < n; ++i)
    {
        float sample = (rL[i] + rR[i]) * 0.5f;
        fifo[(size_t) fifoIndex] = sample;
        fifoL[(size_t) fifoIndex] = rL[i];
        fifoR[(size_t) fifoIndex] = rR[i];
        ++fifoIndex;
        if (fifoIndex >= fftSize)
        {
            fifoIndex = 0;
            std::copy (fifo.begin(), fifo.end(), fftData.begin());
            std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
            fftReady.store (true, std::memory_order_release);
        }
    }
}

void OutputMeter::computeFFTMagnitudes()
{
    if (! fftReady.load (std::memory_order_acquire)) return;
    fftReady.store (false, std::memory_order_release);

    // EMA factor based on speed setting
    static const float emaNew[] = { 0.08f, 0.20f, 0.50f }; // Slow, Medium, Fast
    int spd = juce::jlimit (0, 2, analyzerSpeed.load());
    float newW = emaNew[spd];
    float oldW = 1.0f - newW;
    auto minDb = -80.0f, maxDb = 0.0f;

    // ─── Mono mix spectrum ───
    fftWindow.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fftProcessor.performFrequencyOnlyForwardTransform (fftData.data());
    for (int i = 0; i < fftSize / 2; ++i)
    {
        auto level = juce::Decibels::gainToDecibels (fftData[(size_t)i] / (float) fftSize, minDb);
        float val = juce::jmap (level, minDb, maxDb, 0.0f, 1.0f);
        magnitudes[(size_t)i] = magnitudes[(size_t)i] * oldW + val * newW;
    }

    // ─── Mid = (L+R)/2 ───
    for (int i = 0; i < fftSize; ++i)
        msFftData[(size_t) i] = (fifoL[(size_t) i] + fifoR[(size_t) i]) * 0.5f;
    std::fill (msFftData.begin() + fftSize, msFftData.end(), 0.0f);
    fftWindow.multiplyWithWindowingTable (msFftData.data(), (size_t) fftSize);
    fftProcessor.performFrequencyOnlyForwardTransform (msFftData.data());
    for (int i = 0; i < fftSize / 2; ++i)
    {
        auto level = juce::Decibels::gainToDecibels (msFftData[(size_t)i] / (float) fftSize, minDb);
        float val = juce::jmap (level, minDb, maxDb, 0.0f, 1.0f);
        midMagnitudes[(size_t)i] = midMagnitudes[(size_t)i] * oldW + val * newW;
    }

    // ─── Side = (L-R)/2 ───
    for (int i = 0; i < fftSize; ++i)
        msFftData[(size_t) i] = (fifoL[(size_t) i] - fifoR[(size_t) i]) * 0.5f;
    std::fill (msFftData.begin() + fftSize, msFftData.end(), 0.0f);
    fftWindow.multiplyWithWindowingTable (msFftData.data(), (size_t) fftSize);
    fftProcessor.performFrequencyOnlyForwardTransform (msFftData.data());
    for (int i = 0; i < fftSize / 2; ++i)
    {
        auto level = juce::Decibels::gainToDecibels (msFftData[(size_t)i] / (float) fftSize, minDb);
        float val = juce::jmap (level, minDb, maxDb, 0.0f, 1.0f);
        sideMagnitudes[(size_t)i] = sideMagnitudes[(size_t)i] * oldW + val * newW;
    }
}

void OutputMeter::reset()
{
    preFilterL.reset(); preFilterR.reset(); rlbFilterL.reset(); rlbFilterR.reset();
    momentaryPower = 0; momentaryBlockCount = 0;
    stWriteIdx = 0; stBlockCount = 0;
    intWriteIdx = 0; intBlockCount = 0;
    momentaryLUFS.store (-100.f); shortTermLUFS.store (-100.f);
    integratedLUFS.store (-100.f); truePeak.store (-100.f);
    peakHold = -100.0f; peakHoldCounter = 0;
    correlation.store (1.0f); balance.store (0.0f);
    lRms.store (-100.f); rRms.store (-100.f); stereoWidth.store (0.0f);
    fifoIndex = 0; fftReady.store (false);
    fifoL.fill (0); fifoR.fill (0);
    midMagnitudes.fill (0); sideMagnitudes.fill (0);
    msFftData.fill (0); msfifoIndex = 0;
    // Imager
    imgXover1LP.reset(); imgXover1HP.reset();
    imgXover2LP.reset(); imgXover2HP.reset();
    imgXover3LP.reset(); imgXover3HP.reset();
    soloedBand.store (-1);
    for (auto& bs : bandStereo)
    {
        bs.correlation.store (1.0f); bs.width.store (0.0f);
        bs.balance.store (0.0f); bs.lRms.store (-100.f); bs.rRms.store (-100.f);
    }
}

void OutputMeter::applySolo (juce::AudioBuffer<float>& buffer)
{
    int solo = soloedBand.load();
    if (solo < 0 || solo >= NUM_IMG_BANDS) return;

    int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;

    // The band buffers were filled during process() — copy the soloed band to output
    auto& soloBuf = imgBandBufs[(size_t) solo];
    if (soloBuf.getNumSamples() >= n)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy (buffer.getWritePointer (ch), soloBuf.getReadPointer (ch), n);
    }
}

void OutputMeter::resetIntegrated()
{
    intWriteIdx = 0; intBlockCount = 0;
    integratedLUFS.store (-100.f);
    truePeak.store (-100.f); peakHold = -100.0f; peakHoldCounter = 0;
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
    reorderableStages[5]=std::make_unique<DynamicEQStage>();
    reorderableStages[6]=std::make_unique<DynamicResonanceStage>();
    reorderableStages[7]=std::make_unique<ClipperStage>();
    reorderableStages[8]=std::make_unique<MultibandDynamicsStage>();
    resetStageOrder();
    oversamplingEngine=std::make_unique<OversamplingEngine>();
    outputMeter=std::make_unique<OutputMeter>();
}

void ProcessingEngine::prepare(double sr,int bs)
{
    currentSampleRate=sr;currentBlockSize=bs;
    oversamplingEngine->prepare(sr,bs,2,oversamplingFactor.load());
    double eSR=sr*oversamplingFactor.load(); int eBs=bs*oversamplingFactor.load();
    inputStage->prepare(eSR,eBs);
    for(auto&s:reorderableStages)s->prepare(eSR,eBs);
    limiterStage->prepare(eSR,eBs);
    outputMeter->prepare(sr,bs);
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

    // Master gain BEFORE limiter so limiter catches everything
    double g=masterOutputGain.load(std::memory_order_relaxed);
    if(std::abs(g-1.0)>1e-6) osBlock.multiplyBy(g);

    // ─── Stereo Imager width processing BEFORE limiter ───
    // So the limiter catches any level increase from width > 100%
    if (outputMeter)
    {
        bool anyWidthChanged = false;
        for (int b = 0; b < 4; ++b)
            if (std::abs (outputMeter->getBandWidth(b) - 100.0f) > 0.5f) { anyWidthChanged = true; break; }

        if (anyWidthChanged)
        {
            // Simple M/S global width for now (avoids float/double mismatch with LR crossovers)
            // Weighted average of band widths as global width factor
            float avgWidth = 0;
            for (int b = 0; b < 4; ++b)
                avgWidth += outputMeter->getBandWidth(b);
            avgWidth = avgWidth / 400.0f; // average of 4 bands, /100 to normalize

            int ns = (int)osBlock.getNumSamples();
            auto* l = osBlock.getChannelPointer(0);
            auto* r = osBlock.getChannelPointer(1);
            for (int i = 0; i < ns; ++i)
            {
                double mid  = (l[i] + r[i]) * 0.5;
                double side = (l[i] - r[i]) * 0.5;
                side *= (double) avgWidth;
                l[i] = mid + side;
                r[i] = mid - side;
            }
        }
    }

    limiterStage->process(osBlock);

    // ─── Dithering (TPDF / NJAD / Dark — Airwindows-inspired, MIT license) ───
    {
        int dm = ditherMode.load (std::memory_order_relaxed);
        if (dm > 0)
        {
            // Bit depth: odd indices = 16-bit, even = 24-bit
            bool is16 = (dm == 1 || dm == 3 || dm == 5);
            double lsb = is16 ? (1.0 / 32768.0) : (1.0 / 8388608.0);
            double quantLevels = is16 ? 32768.0 : 8388608.0;
            int ns = (int)osBlock.getNumSamples();
            int nch = (int)osBlock.getNumChannels();

            if (dm <= 2)
            {
                // ─── TPDF: standard triangular dither ───
                for (int ch = 0; ch < nch; ++ch)
                {
                    auto* d = osBlock.getChannelPointer (ch);
                    for (int i = 0; i < ns; ++i)
                    {
                        double r1 = ditherRng.nextDouble() * 2.0 - 1.0;
                        double r2 = ditherRng.nextDouble() * 2.0 - 1.0;
                        d[i] += (r1 + r2) * lsb;
                    }
                }
            }
            else if (dm <= 4)
            {
                // ─── NJAD: noise-shaped dither with error feedback ───
                // Based on Airwindows NotJustAnotherDither concept (MIT)
                // Feeds back the quantization error → shapes noise spectrum
                for (int ch = 0; ch < nch && ch < 2; ++ch)
                {
                    auto* d = osBlock.getChannelPointer (ch);
                    double& err = (ch == 0) ? njadErrorL : njadErrorR;
                    for (int i = 0; i < ns; ++i)
                    {
                        double input = d[i] - err * 0.5; // subtract shaped error
                        double r1 = ditherRng.nextDouble() * 2.0 - 1.0;
                        double r2 = ditherRng.nextDouble() * 2.0 - 1.0;
                        double dithered = input + (r1 + r2) * lsb;
                        // Quantize
                        double quantized = std::round (dithered * quantLevels) / quantLevels;
                        err = quantized - d[i]; // compute new error
                        d[i] = quantized;
                    }
                }
            }
            else // dm == 5 or 6
            {
                // ─── Dark: multi-tap noise shaping → less HF noise ───
                // Based on Airwindows Dark concept (MIT)
                // 3-tap error filter shapes noise away from 2-4kHz
                for (int ch = 0; ch < nch && ch < 2; ++ch)
                {
                    auto* d = osBlock.getChannelPointer (ch);
                    auto* err = (ch == 0) ? darkErrorL : darkErrorR;
                    for (int i = 0; i < ns; ++i)
                    {
                        // 3-tap noise shaping: [1.0, -0.5, 0.1] coefficients
                        // Pushes noise energy toward lower frequencies
                        double shaped = err[0] * 1.0 - err[1] * 0.5 + err[2] * 0.1;
                        double input = d[i] - shaped;
                        double r1 = ditherRng.nextDouble() * 2.0 - 1.0;
                        double r2 = ditherRng.nextDouble() * 2.0 - 1.0;
                        double dithered = input + (r1 + r2) * lsb * 0.7; // slightly less noise
                        double quantized = std::round (dithered * quantLevels) / quantLevels;
                        // Shift error history
                        err[2] = err[1];
                        err[1] = err[0];
                        err[0] = quantized - d[i];
                        d[i] = quantized;
                    }
                }
            }
        }
    }

    oversamplingEngine->downsample(osBlock,block);

    for(int ch=0;ch<nch;++ch)
    { auto*s=doubleBuffer.getReadPointer(ch);auto*d=buffer.getWritePointer(ch);
      for(int i=0;i<n;++i) d[i]=(float)s[i]; }

    // NOTE: outputMeter is now called from processBlock() AFTER auto-match
}

void ProcessingEngine::reset()
{
    inputStage->reset();
    for(auto&s:reorderableStages)s->reset();
    limiterStage->reset();oversamplingEngine->reset();outputMeter->reset();
}

juce::AudioProcessorValueTreeState::ParameterLayout ProcessingEngine::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterBool>("Global_Bypass","Global Bypass",false));
    layout.add(std::make_unique<juce::AudioParameterBool>("Auto_Match","Auto Match",false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Master_Output_Gain","Master Output",juce::NormalisableRange<float>(-12,12,0.1f),0,juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<juce::AudioParameterChoice>("Oversampling","Oversampling",juce::StringArray{"Off","2x","4x","8x"},0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("Dither_Mode","Dither",juce::StringArray{"Off","TPDF 16","TPDF 24","NJAD 16","NJAD 24","Dark 16","Dark 24"},0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("Analyzer_Speed","Analyzer",juce::StringArray{"Slow","Medium","Fast"},1));
    layout.add(std::make_unique<juce::AudioParameterBool>("Show_Ref_Spectrum","Show Ref Spectrum",true));
    layout.add(std::make_unique<juce::AudioParameterFloat>("LUFS_Target","LUFS Target",juce::NormalisableRange<float>(-24,-6,0.1f),-14,juce::AudioParameterFloatAttributes().withLabel("LUFS")));
    // Stereo Imager parameters
    layout.add(std::make_unique<juce::AudioParameterFloat>("IMG_Xover1","Img Xover1",juce::NormalisableRange<float>(20,500,1,0.4f),120));
    layout.add(std::make_unique<juce::AudioParameterFloat>("IMG_Xover2","Img Xover2",juce::NormalisableRange<float>(200,5000,1,0.35f),1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("IMG_Xover3","Img Xover3",juce::NormalisableRange<float>(1000,16000,1,0.3f),8000));
    for (int b = 1; b <= 4; ++b)
    {
        auto p = "IMG_B" + juce::String(b) + "_Width";
        auto l = "B" + juce::String(b) + " Width";
        layout.add(std::make_unique<juce::AudioParameterFloat>(p, l, juce::NormalisableRange<float>(0,200,1), 100));
    }
    inputStage->addParameters(layout);
    for(auto&s:reorderableStages)s->addParameters(layout);
    limiterStage->addParameters(layout);
    return layout;
}

void ProcessingEngine::updateAllParameters(const juce::AudioProcessorValueTreeState& a)
{
    masterOutputGain.store(juce::Decibels::decibelsToGain((double)a.getRawParameterValue("Master_Output_Gain")->load()));
    ditherMode.store((int)a.getRawParameterValue("Dither_Mode")->load());
    if (outputMeter)
        outputMeter->setAnalyzerSpeed ((int) a.getRawParameterValue("Analyzer_Speed")->load());
    int oc=(int)a.getRawParameterValue("Oversampling")->load();
    int nf=1<<oc;
    if(nf!=oversamplingFactor.load()){oversamplingFactor.store(nf);prepare(currentSampleRate,currentBlockSize);}
    inputStage->updateParameters(a);
    for(auto&s:reorderableStages)s->updateParameters(a);
    limiterStage->updateParameters(a);
    // Imager
    if (outputMeter)
    {
        outputMeter->setImagerXover (0, a.getRawParameterValue("IMG_Xover1")->load());
        outputMeter->setImagerXover (1, a.getRawParameterValue("IMG_Xover2")->load());
        outputMeter->setImagerXover (2, a.getRawParameterValue("IMG_Xover3")->load());
        for (int b = 0; b < 4; ++b)
            outputMeter->setBandWidth (b, a.getRawParameterValue("IMG_B" + juce::String(b+1) + "_Width")->load());
    }
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
{
    int def[] = {0,1,2,3,4,5,6,8,7};
    for(int i=0;i<NUM_REORDERABLE;++i)stageOrder[i].store(def[i]);
}

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

float ProcessingEngine::getLUFS()const{return outputMeter->getIntegratedLUFS();}
float ProcessingEngine::getTruePeak()const{return outputMeter->getTruePeak();}

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

void PresetManager::savePreset(const juce::String& name, const juce::String& category)
{
    auto state=apvts.copyState(); auto xml=state.createXml(); if(!xml)return;
    juce::String os;
    for(int i=0;i<9;++i){if(i>0)os+=",";os+=juce::String(stageOrder[i]);}
    xml->setAttribute("stageOrder",os);
    xml->setAttribute("presetName",name);
    xml->setAttribute("category",category);
    xml->writeTo(getUserPresetsFolder().getChildFile(name+".xml"));
    currentPreset=name;
    currentCategory=category;
}

void PresetManager::deletePreset(const juce::String& name)
{
    auto f = getUserPresetsFolder().getChildFile(name + ".xml");
    if (f.existsAsFile())
        f.deleteFile();
    if (currentPreset == name)
    { currentPreset = ""; currentCategory = "General"; }
}

bool PresetManager::loadPreset(const juce::String& name)
{
    auto f=getUserPresetsFolder().getChildFile(name+".xml");
    if(!f.existsAsFile())return false;
    auto xml=juce::XmlDocument::parse(f); if(!xml)return false;
    auto os=xml->getStringAttribute("stageOrder","0,1,2,3,4,5,6,8,7");
    auto tok=juce::StringArray::fromTokens(os,",","");
    if(tok.size()==9) for(int i=0;i<9;++i)stageOrder[i]=tok[i].getIntValue();
    else if(tok.size()==8) {
        std::array<int,8> old8;
        for(int i=0;i<8;++i)old8[i]=tok[i].getIntValue();
        for(int i=0;i<9;++i)stageOrder[i]=i;
        int wp=0;
        for(int i=0;i<8;++i){
            int v=old8[i];
            if(v<5) stageOrder[wp++]=v;
            else { if(wp==5){stageOrder[wp++]=5;} stageOrder[wp++]=v+1; }
        }
        if(wp==8){stageOrder[5]=5;for(int i=8;i>5;--i)stageOrder[i]=stageOrder[i-1];stageOrder[5]=5;}
    }
    else if(tok.size()==7){
        for(int i=0;i<7;++i)stageOrder[i]=tok[i].getIntValue();
        stageOrder[7]=7; stageOrder[8]=8;
    }
    currentCategory = xml->getStringAttribute("category", "General");
    auto tree=juce::ValueTree::fromXml(*xml);
    if(tree.isValid())apvts.replaceState(tree);
    currentPreset=name; return true;
}

void PresetManager::loadInit()
{
    for(auto*p:apvts.processor.getParameters())
        if(auto*r=dynamic_cast<juce::RangedAudioParameter*>(p))
            r->setValueNotifyingHost(r->getDefaultValue());
    stageOrder={0,1,2,3,4,5,6,8,7}; currentPreset="INIT"; currentCategory="General";
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

juce::StringArray PresetManager::getCategories()const
{
    juce::StringArray cats;
    cats.add("General"); cats.add("Mastering"); cats.add("Mixing");
    cats.add("Vocal"); cats.add("Drums"); cats.add("Bass");
    cats.add("Electronic"); cats.add("Acoustic");
    // Also scan existing presets for custom categories
    auto dir = getUserPresetsFolder();
    if (dir.isDirectory())
    {
        for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.xml"))
        {
            auto xml = juce::XmlDocument::parse(f);
            if (xml)
            {
                auto cat = xml->getStringAttribute("category", "General");
                if (!cats.contains(cat)) cats.add(cat);
            }
        }
    }
    return cats;
}

juce::String PresetManager::getPresetCategory(const juce::String& name)const
{
    auto f = getUserPresetsFolder().getChildFile(name + ".xml");
    if (!f.existsAsFile()) return "General";
    auto xml = juce::XmlDocument::parse(f);
    if (!xml) return "General";
    return xml->getStringAttribute("category", "General");
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
      apvts(*this,&undoManager,"EasyMasterState",engine.createParameterLayout()),
      presetManager(apvts)
{
    refFormatManager.registerBasicFormats();
}

EasyMasterProcessor::~EasyMasterProcessor()=default;

void EasyMasterProcessor::prepareToPlay(double sr,int bs)
{
    engine.prepare(sr,bs); engine.updateAllParameters(apvts); setLatencySamples(engine.getTotalLatency());
    // Reset spectrum analysis buffers on SR change
    refFifoPos = 0; masterFifoPos = 0;
    masterMidMags.fill (-100.0f); masterSideMags.fill (-100.0f);
    refMidMags.fill (-100.0f); refSideMags.fill (-100.0f);
    masterFftWork.fill (0); masterFifoL.fill (0); masterFifoR.fill (0);
    refFifoL.fill (0); refFifoR.fill (0); refFftWork.fill (0);
}

void EasyMasterProcessor::releaseResources(){engine.reset();}

void EasyMasterProcessor::processBlock(juce::AudioBuffer<float>& buf,juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    for(auto i=getTotalNumInputChannels();i<getTotalNumOutputChannels();++i)
        buf.clear(i,0,buf.getNumSamples());

    bool bypassed = apvts.getRawParameterValue("Global_Bypass")->load() > 0.5f;
    bool autoMatch = apvts.getRawParameterValue("Auto_Match")->load() > 0.5f;

    if (bypassed)
        return;

    // Measure input loudness (K-weighted power approximation) for auto-match
    float inputLoudness = 0.0f;
    if (autoMatch)
    {
        int n = buf.getNumSamples();
        int nch = juce::jmin (buf.getNumChannels(), 2);
        double sumPower = 0.0;
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* data = buf.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                sumPower += (double)(data[i] * data[i]);
        }
        inputLoudness = (float)(-0.691 + 10.0 * std::log10 (std::max (sumPower / (double)(n * nch), 1e-10)));
        // Smooth convergence — slow enough to avoid pumping
        if (smoothedInputLoudness < -80.0f)
            smoothedInputLoudness = inputLoudness;
        else
            smoothedInputLoudness = smoothedInputLoudness * 0.92f + inputLoudness * 0.08f;
    }
    else
    {
        // Reset input loudness and match gain when off
        smoothedInputLoudness = -100.0f;
        smoothedMatchGain = 1.0f;
    }

    engine.updateAllParameters(apvts);
    analyzerSpeed.store ((int) apvts.getRawParameterValue("Analyzer_Speed")->load());
    setLatencySamples(engine.getTotalLatency());
    engine.process(buf);

    // Auto-match: measure output loudness BEFORE applying match gain (no feedback loop)
    if (autoMatch && smoothedInputLoudness > -80.0f)
    {
        int n = buf.getNumSamples();
        int nch = juce::jmin (buf.getNumChannels(), 2);

        // Measure RAW output loudness (before match gain)
        double sumPower = 0.0;
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* data = buf.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                sumPower += (double)(data[i] * data[i]);
        }
        float outputLoudness = (float)(-0.691 + 10.0 * std::log10 (std::max (sumPower / (double)(n * nch), 1e-10)));

        if (smoothedOutputLoudness < -80.0f)
            smoothedOutputLoudness = outputLoudness;
        else
            smoothedOutputLoudness = smoothedOutputLoudness * 0.92f + outputLoudness * 0.08f;

        // Compensate: difference in LUFS = gain to apply
        float diffDb = smoothedInputLoudness - smoothedOutputLoudness;
        diffDb = juce::jlimit (-6.0f, 6.0f, diffDb);
        float targetGain = juce::Decibels::decibelsToGain (diffDb);

        // Per-sample gain ramp (prevents clicks and artifacts)
        float startGain = smoothedMatchGain;
        float endGain = startGain * 0.95f + targetGain * 0.05f;
        smoothedMatchGain = endGain;

        float gainStep = (endGain - startGain) / (float) n;
        float curGain = startGain;
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            curGain = startGain;
            for (int i = 0; i < n; ++i)
            {
                curGain += gainStep;
                d[i] *= curGain;
            }
        }
    }

    // Metering AFTER auto-match so LUFS shows what user actually hears
    if (auto* om = engine.getOutputMeter())
    {
        om->process (buf);
        om->applySolo (buf);  // band solo monitoring
    }

    // Always measure output loudness (for A/B reference matching)
    if (!autoMatch)
    {
        int n = buf.getNumSamples();
        int nch = juce::jmin (buf.getNumChannels(), 2);
        double sumPower = 0.0;
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* data = buf.getReadPointer (ch);
            for (int i = 0; i < n; ++i) sumPower += (double)(data[i] * data[i]);
        }
        float outLoud = (float)(-0.691 + 10.0 * std::log10 (std::max (sumPower / (double)(n * nch), 1e-10)));
        if (smoothedOutputLoudness < -80.0f) smoothedOutputLoudness = outLoud;
        else smoothedOutputLoudness = smoothedOutputLoudness * 0.8f + outLoud * 0.2f;
    }

    // ─── Master spectrum for comparison ───
    {
        int n = buf.getNumSamples();
        auto* l = buf.getReadPointer (0);
        auto* r = (buf.getNumChannels() > 1) ? buf.getReadPointer (1) : l;
        for (int i = 0; i < n; ++i)
            pushToMasterFFT (l[i], r[i]);
    }

    // ─── FINAL SAFETY CLIP — enforces the most restrictive ceiling ───
    // Global oversampling downsample creates intersample peaks above ceiling.
    // Use the LOWEST ceiling among active modules (clipper or limiter).
    {
        float ceilLin = 1.0f; // default: 0 dBFS

        bool clipOn = apvts.getRawParameterValue ("S7_Clipper_On")->load() > 0.5f;
        bool limOn  = apvts.getRawParameterValue ("S7_Lim_On")->load() > 0.5f;

        if (clipOn)
        {
            float clipCeil = apvts.getRawParameterValue ("S7_Clipper_Ceiling")->load();
            ceilLin = juce::jmin (ceilLin, juce::Decibels::decibelsToGain (clipCeil));
        }
        if (limOn)
        {
            float limCeil = apvts.getRawParameterValue ("S7_Lim_Ceiling")->load();
            ceilLin = juce::jmin (ceilLin, juce::Decibels::decibelsToGain (limCeil));
        }

        int n = buf.getNumSamples();
        int nch = juce::jmin (buf.getNumChannels(), 2);
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = juce::jlimit (-ceilLin, ceilLin, d[i]);
        }
    }

    // ─── Reference spectrum — always analyze when loaded (even without A/B) ───
    if (refLoaded.load())
    {
        int n = buf.getNumSamples();
        int refLen = refBuffer.getNumSamples();
        if (refLen > 0)
        {
            int64_t pos = refPlayPos.load();
            int nch = refBuffer.getNumChannels();
            for (int i = 0; i < n; ++i)
            {
                int idx = (int)((pos + i) % refLen);
                float sL = refBuffer.getSample (0, idx);
                float sR = (nch > 1) ? refBuffer.getSample (1, idx) : sL;
                pushToRefFFT (sL, sR);
            }
            // Advance ref play position for continuous spectrum even without A/B
            if (!abActive.load())
                refPlayPos.store ((pos + n) % refLen);
        }
    }

    // ─── A/B Reference switching ───
    if (abActive.load() && refLoaded.load())
    {
        int n = buf.getNumSamples();
        int refLen = refBuffer.getNumSamples();
        if (refLen > 0)
        {
            // Level matching: match reference LUFS to master output LUFS
            float matchGain = 1.0f;
            if (smoothedOutputLoudness > -80.0f && refLufs > -80.0f)
            {
                float diffDb = smoothedOutputLoudness - refLufs;
                diffDb = juce::jlimit (-6.0f, 6.0f, diffDb); // conservative ±6dB
                float targetGain = juce::Decibels::decibelsToGain (diffDb);
                // Smooth the gain to avoid clicks when switching
                smoothedRefMatchGain = smoothedRefMatchGain * 0.95f + targetGain * 0.05f;
                matchGain = smoothedRefMatchGain;
            }

            int64_t pos = refPlayPos.load();
            int nch = juce::jmin (buf.getNumChannels(), refBuffer.getNumChannels());
            for (int ch = 0; ch < nch; ++ch)
            {
                auto* out = buf.getWritePointer (ch);
                auto* ref = refBuffer.getReadPointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    float s = ref[(int)((pos + i) % refLen)] * matchGain;
                    // Safety soft-clip to prevent DAW distortion
                    if (s > 1.0f) s = 1.0f - 1.0f / (s + 1.0f);
                    else if (s < -1.0f) s = -1.0f + 1.0f / (-s + 1.0f);
                    out[i] = s;
                }
            }
            if (nch == 1 && buf.getNumChannels() > 1)
                buf.copyFrom (1, 0, buf, 0, 0, n);

            refPlayPos.store ((pos + n) % refLen);
        }
    }
}

// ─── Reference Track Methods ───

void EasyMasterProcessor::loadReferenceFile (const juce::File& file)
{
    auto* reader = refFormatManager.createReaderFor (file);
    if (!reader)
        return;

    int numSamples = (int) reader->lengthInSamples;
    int numChannels = (int) reader->numChannels;
    double fileSR = reader->sampleRate;

    refBuffer.setSize (juce::jmin (numChannels, 2), numSamples);
    reader->read (&refBuffer, 0, numSamples, 0, true, numChannels > 1);
    delete reader;

    // Resample if needed
    if (std::abs (fileSR - getSampleRate()) > 1.0 && getSampleRate() > 0)
    {
        double ratio = getSampleRate() / fileSR;
        int newLen = (int)(numSamples * ratio);
        juce::AudioBuffer<float> resampled (refBuffer.getNumChannels(), newLen);
        for (int ch = 0; ch < refBuffer.getNumChannels(); ++ch)
        {
            auto* src = refBuffer.getReadPointer (ch);
            auto* dst = resampled.getWritePointer (ch);
            for (int i = 0; i < newLen; ++i)
            {
                double srcPos = (double) i / ratio;
                int idx = (int) srcPos;
                double frac = srcPos - idx;
                if (idx + 1 < numSamples)
                    dst[i] = (float)((1.0 - frac) * src[idx] + frac * src[idx + 1]);
                else
                    dst[i] = src[juce::jmin (idx, numSamples - 1)];
            }
        }
        refBuffer = std::move (resampled);
    }

    // Measure reference LUFS
    {
        double sum = 0;
        int n = refBuffer.getNumSamples();
        int nch = refBuffer.getNumChannels();
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* d = refBuffer.getReadPointer (ch);
            for (int i = 0; i < n; ++i) sum += d[i] * d[i];
        }
        refLufs = (float)(-0.691 + 10.0 * std::log10 (std::max (sum / (n * nch), 1e-10)));
    }

    refFileName = file.getFileNameWithoutExtension();
    refPlayPos.store (0);

    // Compute waveform peaks for display
    {
        int n = refBuffer.getNumSamples();
        int nch = refBuffer.getNumChannels();
        for (int p = 0; p < WAVEFORM_POINTS; ++p)
        {
            int startSamp = (int)((int64_t) p * n / WAVEFORM_POINTS);
            int endSamp = (int)((int64_t)(p + 1) * n / WAVEFORM_POINTS);
            float peak = 0.0f;
            for (int ch = 0; ch < nch; ++ch)
            {
                auto* data = refBuffer.getReadPointer (ch);
                for (int i = startSamp; i < endSamp && i < n; ++i)
                    peak = juce::jmax (peak, std::abs (data[i]));
            }
            refWaveformPeaks[(size_t) p] = peak;
        }
    }

    // Pre-compute reference spectrum from a representative section (middle 2048 samples)
    {
        int n = refBuffer.getNumSamples();
        int start = juce::jmax (0, n / 2 - REF_FFT_SIZE / 2);
        int nch = refBuffer.getNumChannels();
        refMidMags.fill (-100.0f); refSideMags.fill (-100.0f);

        // Average multiple windows for stable spectrum
        int numWindows = juce::jmin (8, n / REF_FFT_SIZE);
        for (int w = 0; w < juce::jmax (1, numWindows); ++w)
        {
            int offset = (n / (numWindows + 1)) * (w + 1);
            offset = juce::jlimit (0, n - REF_FFT_SIZE, offset);

            for (int i = 0; i < REF_FFT_SIZE; ++i)
            {
                refFifoL[(size_t) i] = refBuffer.getSample (0, offset + i);
                refFifoR[(size_t) i] = (nch > 1) ? refBuffer.getSample (1, offset + i) : refFifoL[(size_t) i];
            }
            computeSpectrum (refFifoL.data(), refFifoR.data(), refMidMags, refSideMags);
        }
    }

    refLoaded.store (true);
}

void EasyMasterProcessor::clearReference()
{
    refLoaded.store (false);
    abActive.store (false);
    refBuffer.setSize (0, 0);
    refFileName = "";
    refWaveformPeaks.fill (0);
}

float EasyMasterProcessor::getRefDurationSeconds() const
{
    if (!refLoaded.load() || getSampleRate() <= 0) return 0;
    return (float) refBuffer.getNumSamples() / (float) getSampleRate();
}

float EasyMasterProcessor::getRefPlayPositionNorm() const
{
    if (!refLoaded.load()) return 0;
    int len = refBuffer.getNumSamples();
    if (len <= 0) return 0;
    return (float)(refPlayPos.load() % len) / (float) len;
}

void EasyMasterProcessor::setRefPlayPosition (float normPos)
{
    if (!refLoaded.load()) return;
    int len = refBuffer.getNumSamples();
    int64_t pos = (int64_t)(juce::jlimit (0.0f, 1.0f, normPos) * (float) len);
    refPlayPos.store (pos);
}

void EasyMasterProcessor::pushToRefFFT (float sampleL, float sampleR)
{
    refFifoL[(size_t) refFifoPos] = sampleL;
    refFifoR[(size_t) refFifoPos] = sampleR;
    ++refFifoPos;
    // 75% overlap: trigger every 2048 samples (same rate as OutputMeter's 2048-point FFT)
    static constexpr int HOP = 2048;
    if (refFifoPos % HOP == 0)
    {
        computeSpectrum (refFifoL.data(), refFifoR.data(), refMidMags, refSideMags);
        refSpecReady.store (true);
    }
    if (refFifoPos >= REF_FFT_SIZE)
        refFifoPos = 0;
}

void EasyMasterProcessor::pushToMasterFFT (float sampleL, float sampleR)
{
    masterFifoL[(size_t) masterFifoPos] = sampleL;
    masterFifoR[(size_t) masterFifoPos] = sampleR;
    ++masterFifoPos;
    static constexpr int HOP = 2048;
    if (masterFifoPos % HOP == 0)
    {
        computeSpectrum (masterFifoL.data(), masterFifoR.data(), masterMidMags, masterSideMags);
    }
    if (masterFifoPos >= REF_FFT_SIZE)
        masterFifoPos = 0;
}

void EasyMasterProcessor::computeSpectrum (const float* fifoL, const float* fifoR,
                                            std::array<float, REF_FFT_HALF>& midMags,
                                            std::array<float, REF_FFT_HALF>& sideMags)
{
    // Hann window has coherent gain = 0.5, so normalize with 2/N
    float invN = 2.0f / (float) REF_FFT_SIZE;
    // EMA factor synced with global analyzer speed
    static const float emaNew[] = { 0.08f, 0.20f, 0.50f };
    int spd = juce::jlimit (0, 2, analyzerSpeed.load());
    float smoothNew = emaNew[spd];
    float smoothOld = 1.0f - smoothNew;

    // ─── Mid = (L+R)/2 ───
    for (int i = 0; i < REF_FFT_SIZE; ++i)
        masterFftWork[(size_t) i] = (fifoL[i] + fifoR[i]) * 0.5f;
    std::fill (masterFftWork.begin() + REF_FFT_SIZE, masterFftWork.end(), 0.0f);
    refWindow.multiplyWithWindowingTable (masterFftWork.data(), (size_t) REF_FFT_SIZE);
    refFft.performFrequencyOnlyForwardTransform (masterFftWork.data());
    for (int i = 0; i < REF_FFT_HALF; ++i)
    {
        float db = juce::Decibels::gainToDecibels (masterFftWork[(size_t) i] * invN, -100.0f);
        midMags[(size_t) i] = midMags[(size_t) i] * smoothOld + db * smoothNew;
    }

    // ─── Side = (L-R)/2 ───
    for (int i = 0; i < REF_FFT_SIZE; ++i)
        masterFftWork[(size_t) i] = (fifoL[i] - fifoR[i]) * 0.5f;
    std::fill (masterFftWork.begin() + REF_FFT_SIZE, masterFftWork.end(), 0.0f);
    refWindow.multiplyWithWindowingTable (masterFftWork.data(), (size_t) REF_FFT_SIZE);
    refFft.performFrequencyOnlyForwardTransform (masterFftWork.data());
    for (int i = 0; i < REF_FFT_HALF; ++i)
    {
        float db = juce::Decibels::gainToDecibels (masterFftWork[(size_t) i] * invN, -100.0f);
        sideMags[(size_t) i] = sideMags[(size_t) i] * smoothOld + db * smoothNew;
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
    auto os=tree.getProperty("stageOrder","0,1,2,3,4,5,6,8,7").toString();
    auto tok=juce::StringArray::fromTokens(os,",","");
    if(tok.size()==ProcessingEngine::NUM_REORDERABLE)
    {
        std::array<int,ProcessingEngine::NUM_REORDERABLE> o;
        for(int i=0;i<ProcessingEngine::NUM_REORDERABLE;++i)o[i]=tok[i].getIntValue();
        engine.setStageOrder(o);
    }
    else if(tok.size()==8)
    {
        // Migrate from 8 reorderable: insert DynEQ(5) at pos 5, remap old 5-7 → 6-8
        std::array<int,8> old8;
        for(int i=0;i<8;++i)old8[i]=tok[i].getIntValue();
        std::array<int,ProcessingEngine::NUM_REORDERABLE> o={0,1,2,3,4,5,6,8,7};
        int wp=0; bool inserted5=false;
        for(int i=0;i<8&&wp<9;++i){
            int v=old8[i];
            if(!inserted5&&(v>=5||wp>=5)){o[wp++]=5;inserted5=true;}
            if(wp<9) o[wp++]=(v<5)?v:v+1;
        }
        if(!inserted5&&wp<9)o[wp++]=5;
        engine.setStageOrder(o);
    }
    else if(tok.size()==7)
    {
        std::array<int,ProcessingEngine::NUM_REORDERABLE> o;
        for(int i=0;i<7;++i)o[i]=tok[i].getIntValue();
        o[7]=7; o[8]=8;
        engine.setStageOrder(o);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new EasyMasterProcessor();}
