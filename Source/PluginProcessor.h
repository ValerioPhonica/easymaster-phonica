#pragma once
#include <JuceHeader.h>

// ═══════════════════════════════════════════════════════════════
//  EASY MASTER — Phonica School
//  Professional Mastering Plugin — JUCE 7 / C++17
//  All headers consolidated for GitHub upload simplicity
// ═══════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────
//  PROCESSING STAGE — Abstract base
// ─────────────────────────────────────────────────────────────

class ProcessingStage
{
public:
    enum class StageID
    {
        Input = 0, PultecEQ, Compressor, Saturation,
        OutputEQ, Filter, DynamicResonance, Clipper, Limiter,
        NumStages
    };

    ProcessingStage (StageID id, const juce::String& name, bool reorderable = true)
        : stageID (id), stageName (name), canReorder (reorderable) {}
    virtual ~ProcessingStage() = default;

    virtual void prepare (double sampleRate, int samplesPerBlock) = 0;
    virtual void process (juce::dsp::AudioBlock<double>& block) = 0;
    virtual void reset() = 0;
    virtual void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) = 0;
    virtual void updateParameters (const juce::AudioProcessorValueTreeState& apvts) = 0;
    virtual int getLatencySamples() const { return 0; }

    bool isEnabled() const { return enabled.load(); }
    void setEnabled (bool on) { enabled.store (on); }
    StageID getStageID() const { return stageID; }
    const juce::String& getStageName() const { return stageName; }
    bool isReorderable() const { return canReorder; }

    struct MeterData
    {
        std::atomic<float> inputLevelL  { -100.0f };
        std::atomic<float> inputLevelR  { -100.0f };
        std::atomic<float> outputLevelL { -100.0f };
        std::atomic<float> outputLevelR { -100.0f };
        std::atomic<float> gainReduction { 0.0f };
    };
    const MeterData& getMeterData() const { return meterData; }

protected:
    void updateInputMeters (const juce::dsp::AudioBlock<double>& block)
    {
        if (block.getNumChannels() < 2) return;
        float pL = 0, pR = 0;
        auto* L = block.getChannelPointer(0); auto* R = block.getChannelPointer(1);
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        { pL = std::max(pL,(float)std::abs(L[i])); pR = std::max(pR,(float)std::abs(R[i])); }
        meterData.inputLevelL.store(juce::Decibels::gainToDecibels(pL,-100.f));
        meterData.inputLevelR.store(juce::Decibels::gainToDecibels(pR,-100.f));
    }
    void updateOutputMeters (const juce::dsp::AudioBlock<double>& block)
    {
        if (block.getNumChannels() < 2) return;
        float pL = 0, pR = 0;
        auto* L = block.getChannelPointer(0); auto* R = block.getChannelPointer(1);
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        { pL = std::max(pL,(float)std::abs(L[i])); pR = std::max(pR,(float)std::abs(R[i])); }
        meterData.outputLevelL.store(juce::Decibels::gainToDecibels(pL,-100.f));
        meterData.outputLevelR.store(juce::Decibels::gainToDecibels(pR,-100.f));
    }

    StageID stageID;
    juce::String stageName;
    bool canReorder;
    std::atomic<bool> enabled { true };
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    MeterData meterData;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessingStage)
};

// ─────────────────────────────────────────────────────────────
//  LINEAR PHASE FIR UTILITY — Converts IIR magnitude response to FIR
// ─────────────────────────────────────────────────────────────

class LinearPhaseFIR
{
public:
    // 1024 taps → 512 samples latency (~10ms @ 48kHz — fine for mastering)
    static constexpr int FIR_SIZE = 1024;
    static constexpr int FIR_ORDER = 10; // 2^10 = 1024

    LinearPhaseFIR() = default;
    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::dsp::AudioBlock<double>& block);
    void reset();

    void designFromIIRMagnitude (const std::vector<juce::dsp::IIR::Coefficients<double>::Ptr>& coeffs, double sampleRate);

    int getLatency() const { return active ? FIR_SIZE / 2 : 0; }
    bool isActive() const { return active; }

private:
    juce::dsp::FIR::Filter<double> firL, firR;
    juce::dsp::FIR::Coefficients<double>::Ptr sharedCoeffs;  // pre-allocated, shared
    bool active = false;
    bool prepared = false;
    double sr = 44100.0;

    // Pre-allocated buffers — zero allocations in audio thread
    juce::dsp::FFT designFFT { FIR_ORDER };
    std::array<float, FIR_SIZE * 2> fftWorkBuf {};
    std::array<double, FIR_SIZE> kernelBuf {};

    // Crossfade state to avoid clicks on coefficient change
    static constexpr int XFADE_LEN = 64;
    int xfadeSamplesLeft = 0;
};

// ─────────────────────────────────────────────────────────────
//  STAGE 1: INPUT — SSL-style widener + M/S
// ─────────────────────────────────────────────────────────────

class InputStage : public ProcessingStage
{
public:
    InputStage() : ProcessingStage (StageID::Input, "Input", false) {}
    void prepare (double sr, int bs) override;
    void process (juce::dsp::AudioBlock<double>& block) override;
    void reset() override;
    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& apvts) override;
    int getLatencySamples() const override;
    float getCorrelation() const { return correlation.load(); }

private:
    std::atomic<double> inputGain{1.0}, midGain{1.0}, sideGain{1.0};
    std::atomic<bool> stageOn{true};
    std::atomic<float> correlation{1.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputStage)
};

// ─────────────────────────────────────────────────────────────
//  STAGE 2: PULTEC EQ — Dual section (EQP-1A + MEQ-5)
// ─────────────────────────────────────────────────────────────

class PultecEQStage : public ProcessingStage
{
public:
    PultecEQStage() : ProcessingStage (StageID::PultecEQ, "Pultec EQ", true) {}
    void prepare (double sr, int bs) override;
    void process (juce::dsp::AudioBlock<double>& block) override;
    void reset() override;
    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& apvts) override;

    // EQ curve for display
    double getMagnitudeAtFreq (double freq) const;

    // FFT for spectrum analyzer
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    void pushSampleToFFT (float sample);
    bool isFFTReady() const { return fftReady.load (std::memory_order_acquire); }
    void computeFFTMagnitudes();
    const std::array<float, fftSize / 2>& getMagnitudes() const { return fftMagnitudes; }

private:
    // ─── EQP-1A circuit model ───
    // Low Boost: shelf + inductor overshoot resonance
    juce::dsp::IIR::Filter<double> lowShelfL, lowShelfR;      // main shelf
    juce::dsp::IIR::Filter<double> lowResonanceL, lowResonanceR; // LC overshoot peak

    // Low Atten: narrower shelf (creates dip above freq when combined with boost)
    juce::dsp::IIR::Filter<double> lowAttenL, lowAttenR;

    // High Boost: LC resonant peak (bell) + asymmetry shelf
    juce::dsp::IIR::Filter<double> highPeakL, highPeakR;      // main bell
    juce::dsp::IIR::Filter<double> highAirL, highAirR;        // "air" shelf above peak

    // High Atten: RC shelf cut at separate frequency
    juce::dsp::IIR::Filter<double> highAttenL, highAttenR;

    // MEQ-5 bands + inductor overshoot
    juce::dsp::IIR::Filter<double> lowMidL, lowMidR, lowMidSkirtL, lowMidSkirtR;
    juce::dsp::IIR::Filter<double> midDipL, midDipR, midDipSkirtL, midDipSkirtR;
    juce::dsp::IIR::Filter<double> highMidL, highMidR, highMidSkirtL, highMidSkirtR;

    // Transformer model: gentle HF rolloff
    juce::dsp::IIR::Filter<double> xfmrL, xfmrR;

    // Parameters
    std::atomic<bool> stageOn{true};
    std::atomic<float> lowBoostFreq{60}, lowBoostGain{0}, lowAttenGain{0};
    std::atomic<float> highBoostFreq{3000}, highBoostGain{0}, highAttenGain{0}, highAttenFreq{10000}, highAttenBW{5};
    std::atomic<float> lowMidFreq{200}, lowMidGain{0}, midDipFreq{1000}, midDipGain{0};
    std::atomic<float> highMidFreq{1500}, highMidGain{0};

    void updateFilters();

    // Tube 12AX7 waveshaping
    double tubeSaturate (double x) const;

    // FFT internals
    juce::dsp::FFT fftProcessor { fftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> fftFifo {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize / 2> fftMagnitudes {};
    int fftFifoIndex = 0;
    std::atomic<bool> fftReady { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PultecEQStage)
};

// ─────────────────────────────────────────────────────────────
//  STAGE 3: COMPRESSOR — 4 models (VCA/Opto/FET/Vari-Mu)
// ─────────────────────────────────────────────────────────────

class CompressorStage : public ProcessingStage
{
public:
    CompressorStage() : ProcessingStage (StageID::Compressor, "Compressor", true) {}
    void prepare (double sr, int bs) override;
    void process (juce::dsp::AudioBlock<double>& block) override;
    void reset() override;
    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& apvts) override;

private:
    double envelope = 0.0, attackCoeff = 0.0, releaseCoeff = 0.0;
    juce::dsp::IIR::Filter<double> scHpL, scHpR;
    std::atomic<bool> stageOn{true}, autoRelease{false};
    std::atomic<int> model{0};
    std::atomic<float> threshold{-20}, ratio{4}, attackMs{10}, releaseMs{100};
    std::atomic<float> makeupGain{0}, mix{100}, scHpFreq{20};
    double computeGainReduction (double inputLevel);
    void updateCoefficients();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorStage)
};

// ─────────────────────────────────────────────────────────────
//  STAGE 4: SATURATION — 5 types, multiband (4 bands)
// ─────────────────────────────────────────────────────────────

class SaturationStage : public ProcessingStage
{
public:
    SaturationStage() : ProcessingStage (StageID::Saturation, "Saturation", true) {}
    void prepare (double sr, int bs) override;
    void process (juce::dsp::AudioBlock<double>& block) override;
    void reset() override;
    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& apvts) override;

    // For UI: per-band RMS levels in dB
    std::array<std::atomic<float>, 4> bandRmsLevels {};

    // FFT for spectrum analyzer
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder; // 2048
    void pushSampleToFFT (float sample);
    bool isFFTReady() const { return fftReady.load (std::memory_order_acquire); }
    void computeFFTMagnitudes();
    const std::array<float, fftSize / 2>& getMagnitudes() const { return magnitudes; }

    int getMode() const { return mode.load(); }

private:
    juce::dsp::LinkwitzRileyFilter<double> xover1LP, xover1HP, xover2LP, xover2HP, xover3LP, xover3HP;
    std::array<juce::AudioBuffer<double>, 4> bandBuffers;
    juce::AudioBuffer<double> tempBuffer;
    std::atomic<bool> stageOn{true};
    std::atomic<int> mode{0}, satType{0};
    std::atomic<float> drive{0}, bits{16}, rate{44100}, output{0}, blend{100};
    std::atomic<float> xoverFreq1{120}, xoverFreq2{1000}, xoverFreq3{5000};
    std::atomic<int> xoverMode{0};
    struct BandParams {
        std::atomic<int> type{0};
        std::atomic<float> drive{0}, bits{16}, rate{44100}, output{0}, blend{100};
        std::atomic<bool> solo{false}, mute{false};
    };
    std::array<BandParams, 4> bandParams;
    double saturateSample (double input, int type, double driveLinear, double bitsVal, double rateVal);

    // FFT internals
    juce::dsp::FFT fftProcessor { fftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> fifo {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize / 2> magnitudes {};
    int fifoIndex = 0;
    std::atomic<bool> fftReady { false };

    // Sample rate reduction state per-band
    std::array<double, 4> srHoldSample { 0, 0, 0, 0 };
    std::array<double, 4> srCounter { 0, 0, 0, 0 };
    double globalSRHold = 0, globalSRCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SaturationStage)
};

// ─────────────────────────────────────────────────────────────
//  STAGE 5: OUTPUT EQ — 3-band (HS / LS / Mid)
// ─────────────────────────────────────────────────────────────

class OutputEQStage : public ProcessingStage
{
public:
    OutputEQStage() : ProcessingStage (StageID::OutputEQ, "Output EQ", true) {}
    void prepare (double sr, int bs) override;
    void process (juce::dsp::AudioBlock<double>& block) override;
    void reset() override;
    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& apvts) override;

    // EQ curve for FabFilter-style display
    double getMagnitudeAtFreq (double freq) const;

    // FFT for spectrum
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    void pushSampleToFFT (float sample);
    bool isFFTReady() const { return fftReady.load (std::memory_order_acquire); }
    void computeFFTMagnitudes();
    const std::array<float, fftSize / 2>& getMagnitudes() const { return fftMagnitudes; }

    // Band info for UI node display (5 bands: LS, LM, Mid, HM, HS)
    static constexpr int NUM_BANDS = 5;
    struct BandInfo { float freq; float gain; float q; int type; }; // type: 0=LS, 1=Peak, 2=HS
    BandInfo getBandInfo (int band) const;

private:
    // 5 bands: Low Shelf, Low-Mid Peak, Mid Peak, High-Mid Peak, High Shelf
    juce::dsp::IIR::Filter<double> bandL[NUM_BANDS], bandR[NUM_BANDS];
    std::atomic<bool> stageOn{true};

    // Band params: [0]=LS, [1]=LM, [2]=Mid, [3]=HM, [4]=HS
    std::atomic<float> freq[NUM_BANDS], gain[NUM_BANDS], q[NUM_BANDS];

    void updateFilters();

    // FFT
    juce::dsp::FFT fftProcessor { fftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> fftFifo {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize / 2> fftMagnitudes {};
    int fftFifoIndex = 0;
    std::atomic<bool> fftReady { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputEQStage)
};

// ─────────────────────────────────────────────────────────────
//  STAGE 6: HP/LP FILTERS — Variable slope
// ─────────────────────────────────────────────────────────────

class FilterStage : public ProcessingStage
{
public:
    FilterStage() : ProcessingStage (StageID::Filter, "HP/LP Filter", true) {}
    void prepare (double sr, int bs) override;
    void process (juce::dsp::AudioBlock<double>& block) override;
    void reset() override;
    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& apvts) override;
    int getLatencySamples() const override;

private:
    static constexpr int MAX_STAGES = 4;
    std::array<juce::dsp::IIR::Filter<double>, MAX_STAGES> hpL, hpR, lpL, lpR;
    std::atomic<bool> stageOn{true}, hpOn{false}, lpOn{false};
    std::atomic<float> hpFreq{30}, lpFreq{18000};
    std::atomic<int> hpSlope{1}, lpSlope{1}, filterMode{0};
    void updateFilters();
    void rebuildLinearPhase();

    // Linear phase FIR
    LinearPhaseFIR linPhaseHP, linPhaseLP;
    float lastHPFreq = -1, lastLPFreq = -1;
    int lastHPSlope = -1, lastLPSlope = -1;
    bool lastHPOn = false, lastLPOn = false;
    bool linPhaseBuilt = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterStage)
};

// ─────────────────────────────────────────────────────────────
//  STAGE 6B: DYNAMIC RESONANCE — Soothe-style spectral suppressor
// ─────────────────────────────────────────────────────────────

class DynamicResonanceStage : public ProcessingStage
{
public:
    DynamicResonanceStage() : ProcessingStage (StageID::DynamicResonance, "Dynamic Resonance", true) {}
    void prepare (double sr, int bs) override;
    void process (juce::dsp::AudioBlock<double>& block) override;
    void reset() override;
    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& apvts) override;

    // For UI display: per-band GR in dB (negative = cutting)
    static constexpr int NUM_BANDS = 24;
    std::array<std::atomic<float>, NUM_BANDS> bandGR {};  // dB values for UI
    float getBandFreq (int band) const;

private:
    // 24 logarithmically-spaced notch filter bands
    struct Band
    {
        juce::dsp::IIR::Filter<double> filterL, filterR;
        double centerFreq = 1000.0;
        double envelope = 0.0;     // smoothed magnitude at this freq
        double avgMag = 0.0;       // average magnitude for comparison
        double currentGainDb = 0.0;
    };
    std::array<Band, NUM_BANDS> bands;

    // FFT for spectral analysis only (not for processing)
    static constexpr int FFT_ORDER = 11;
    static constexpr int FFT_SIZE = 1 << FFT_ORDER;
    juce::dsp::FFT analysisFft { FFT_ORDER };
    std::array<float, FFT_SIZE * 2> analysisBuffer {};
    std::array<float, FFT_SIZE> analysisWindow {};
    std::array<float, FFT_SIZE / 2 + 1> spectrum {};
    juce::AudioBuffer<float> collectBuffer;
    int collectPos = 0;

    // Parameters
    std::atomic<bool>  stageOn      { true };
    std::atomic<float> depth        { 0.0f };
    std::atomic<float> selectivity  { 50.0f };
    std::atomic<float> sharpness    { 50.0f };
    std::atomic<float> speed        { 50.0f };
    std::atomic<float> lowFreq      { 200.0f };
    std::atomic<float> highFreq     { 12000.0f };

    void analyzeSpectrum();
    void updateBandFilters();
    int freqToBin (float freq) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamicResonanceStage)
};

// ─────────────────────────────────────────────────────────────
//  STAGE 7A: CLIPPER — Hard / Soft / Analog
// ─────────────────────────────────────────────────────────────

class ClipperStage : public ProcessingStage
{
public:
    ClipperStage() : ProcessingStage (StageID::Clipper, "Clipper", true) {}
    void prepare (double sr, int bs) override;
    void process (juce::dsp::AudioBlock<double>& block) override;
    void reset() override;
    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& apvts) override;

private:
    std::atomic<bool> stageOn{true};
    std::atomic<float> ceiling{-0.3f};
    std::atomic<int> style{0};
    double clipSample (double input, double ceilLinear, int clipStyle);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipperStage)
};

// ─────────────────────────────────────────────────────────────
//  STAGE 7B: TRUE PEAK LIMITER — Lookahead + ISP
// ─────────────────────────────────────────────────────────────

class LimiterStage : public ProcessingStage
{
public:
    LimiterStage() : ProcessingStage (StageID::Limiter, "Limiter", false) {}
    void prepare (double sr, int bs) override;
    void process (juce::dsp::AudioBlock<double>& block) override;
    void reset() override;
    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& apvts) override;
    int getLatencySamples() const override;

private:
    juce::AudioBuffer<double> delayBuffer;
    int delayWritePos = 0, lookaheadSamples = 0;
    double grEnvelope = 1.0, attackCoeff = 0.0, releaseCoeff = 0.0;
    std::array<double, 16> truePeakHistory {};
    int tpHistoryPos = 0;
    std::atomic<bool> stageOn{true}, autoRelease{true};
    std::atomic<float> inputGain{0}, ceilingDb{-0.3f}, releaseMs{100}, lookaheadMs{1.0f};
    std::atomic<int> style{0};
    double detectTruePeak (double sample);
    double computeGain (double peakLevel, double ceiling);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LimiterStage)
};

// ─────────────────────────────────────────────────────────────
//  OVERSAMPLING ENGINE
// ─────────────────────────────────────────────────────────────

class OversamplingEngine
{
public:
    OversamplingEngine() = default;
    void prepare (double sr, int bs, int numCh, int factor);
    juce::dsp::AudioBlock<double> upsample (juce::dsp::AudioBlock<double>& input);
    void downsample (juce::dsp::AudioBlock<double>& oversampled, juce::dsp::AudioBlock<double>& output);
    void reset();
    int getLatency() const;
private:
    std::unique_ptr<juce::dsp::Oversampling<double>> oversampler;
    int currentFactor = 1;
    bool prepared = false;
};

// ─────────────────────────────────────────────────────────────
//  OUTPUT METERING — Loudness + Spectrum + Stereo (iZotope Insight style)
// ─────────────────────────────────────────────────────────────

class OutputMeter
{
public:
    OutputMeter() = default;
    void prepare (double sampleRate, int samplesPerBlock);
    void process (juce::AudioBuffer<float>& buffer);
    void applySolo (juce::AudioBuffer<float>& buffer);  // apply band solo to output
    void reset();

    // Loudness
    float getMomentaryLUFS() const  { return momentaryLUFS.load(); }
    float getShortTermLUFS() const  { return shortTermLUFS.load(); }
    float getIntegratedLUFS() const { return integratedLUFS.load(); }
    float getTruePeak() const       { return truePeak.load(); }

    // Global stereo
    float getCorrelation() const  { return correlation.load(); }
    float getBalance() const      { return balance.load(); }
    float getLRms() const         { return lRms.load(); }
    float getRRms() const         { return rRms.load(); }
    float getStereoWidth() const  { return stereoWidth.load(); }

    // Multiband stereo (4 bands)
    static constexpr int NUM_IMG_BANDS = 4;
    struct BandStereo {
        std::atomic<float> correlation { 1.0f };
        std::atomic<float> width { 0.0f };
        std::atomic<float> balance { 0.0f };
        std::atomic<float> lRms { -100.f };
        std::atomic<float> rRms { -100.f };
    };
    const BandStereo& getBandStereo (int band) const { return bandStereo[(size_t) band]; }

    // Band solo: -1 = off, 0-3 = solo that band
    void setSoloedBand (int band) { soloedBand.store (band); }
    int getSoloedBand() const { return soloedBand.load(); }

    // Imager crossover frequencies (for display)
    float getImagerXover (int idx) const { return imagerXover[(size_t) idx].load(); }
    void setImagerXover (int idx, float freq) { imagerXover[(size_t) idx].store (freq); }

    // Per-band width (0-200, 100=original)
    void setBandWidth (int band, float w) { bandWidthValues[(size_t) band].store (w); }
    float getBandWidth (int band) const { return bandWidthValues[(size_t) band].load(); }

    // Output FFT
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder; // 2048
    bool isFFTReady() const { return fftReady.load (std::memory_order_acquire); }
    void computeFFTMagnitudes();
    const std::array<float, fftSize / 2>& getMagnitudes() const { return magnitudes; }

    void resetIntegrated();

private:
    // K-weighting filters
    juce::dsp::IIR::Filter<float> preFilterL, preFilterR, rlbFilterL, rlbFilterR;

    // Momentary (400ms window)
    double momentaryPower = 0.0;
    int momentaryBlockCount = 0;
    int blocksFor400ms = 1;

    // Short-term (3s window, sliding)
    static constexpr int MAX_ST_BLOCKS = 256;
    std::array<double, MAX_ST_BLOCKS> stPowerRing {};
    int stWriteIdx = 0;
    int stBlockCount = 0;
    int blocksFor3s = 1;

    // Integrated (gated, bounded history)
    static constexpr int MAX_INT_BLOCKS = 4096;
    std::array<double, MAX_INT_BLOCKS> intPowerRing {};
    int intWriteIdx = 0;
    int intBlockCount = 0;

    // Atomics for UI
    std::atomic<float> momentaryLUFS{-100.f}, shortTermLUFS{-100.f}, integratedLUFS{-100.f};
    std::atomic<float> truePeak{-100.f};
    std::atomic<float> correlation{1.0f}, balance{0.0f};
    std::atomic<float> lRms{-100.f}, rRms{-100.f}, stereoWidth{0.0f};

    // True peak decay
    float peakHold = -100.0f;
    int peakHoldCounter = 0;
    int peakHoldSamples = 0; // ~2 seconds

    // FFT
    juce::dsp::FFT fftProcessor { fftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> fifo {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize / 2> magnitudes {};
    int fifoIndex = 0;
    std::atomic<bool> fftReady { false };

    // ─── Multiband Imager ───
    // 3 crossover points → 4 bands (default: 120, 1000, 8000)
    std::array<std::atomic<float>, 3> imagerXover;
    juce::dsp::LinkwitzRileyFilter<float> imgXover1LP, imgXover1HP, imgXover2LP, imgXover2HP, imgXover3LP, imgXover3HP;
    std::array<juce::AudioBuffer<float>, 4> imgBandBufs;
    juce::AudioBuffer<float> imgTempBuf;
    std::array<BandStereo, NUM_IMG_BANDS> bandStereo;
    std::atomic<int> soloedBand { -1 };
    std::array<std::atomic<float>, NUM_IMG_BANDS> bandWidthValues;

    double sampleRate = 44100.0;
    int blockSize = 512;
};

// ─────────────────────────────────────────────────────────────
//  PROCESSING ENGINE — Reorderable stage chain
// ─────────────────────────────────────────────────────────────

class ProcessingEngine
{
public:
    ProcessingEngine();
    void prepare (double sampleRate, int samplesPerBlock);
    void process (juce::AudioBuffer<float>& buffer);
    void reset();

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateAllParameters (const juce::AudioProcessorValueTreeState& apvts);

    static constexpr int NUM_REORDERABLE = 7;

    std::array<int, NUM_REORDERABLE> getStageOrder() const;
    void setStageOrder (const std::array<int, NUM_REORDERABLE>& newOrder);
    void swapStages (int posA, int posB);
    void moveStage (int fromPos, int toPos);
    void resetStageOrder();

    ProcessingStage* getStage (ProcessingStage::StageID id);
    ProcessingStage* getStageAtPosition (int position);
    int getTotalLatency() const;
    float getLUFS() const;
    float getTruePeak() const;
    OutputMeter* getOutputMeter() { return outputMeter.get(); }

private:
    std::unique_ptr<InputStage> inputStage;
    std::unique_ptr<LimiterStage> limiterStage;
    std::array<std::unique_ptr<ProcessingStage>, NUM_REORDERABLE> reorderableStages;
    std::array<std::atomic<int>, NUM_REORDERABLE> stageOrder;
    juce::SpinLock orderLock;
    juce::AudioBuffer<double> doubleBuffer;
    std::unique_ptr<OversamplingEngine> oversamplingEngine;
    std::unique_ptr<OutputMeter> outputMeter;
    std::atomic<int> oversamplingFactor{1};
    std::atomic<double> masterOutputGain{1.0};
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
};

// ─────────────────────────────────────────────────────────────
//  PRESET MANAGER — XML save/load
// ─────────────────────────────────────────────────────────────

class PresetManager
{
public:
    PresetManager (juce::AudioProcessorValueTreeState& apvts);
    void savePreset (const juce::String& name);
    void deletePreset (const juce::String& name);
    bool loadPreset (const juce::String& name);
    void loadInit();
    juce::StringArray getPresetList() const;
    juce::String getCurrentPresetName() const { return currentPreset; }
    juce::File getUserPresetsFolder() const;
    void setStageOrder (const std::array<int, 7>& order) { stageOrder = order; }
    std::array<int, 7> getStageOrder() const { return stageOrder; }
private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentPreset { "INIT" };
    std::array<int, 7> stageOrder { 0,1,2,3,4,5,6 };
};

// ─────────────────────────────────────────────────────────────
//  LICENSE MANAGER — PHONICA-XXXX-XXXX-XXXX
// ─────────────────────────────────────────────────────────────

class LicenseManager
{
public:
    LicenseManager();
    bool isActivated() const { return activated; }
    bool activate (const juce::String& serial);
    void deactivate();
private:
    bool activated = false;
    juce::String currentSerial;
    juce::File getLicenseFile() const;
    bool validateFormat (const juce::String& serial) const;
    void loadStoredLicense();
    void saveLicense();
};

// ─────────────────────────────────────────────────────────────
//  PLUGIN PROCESSOR
// ─────────────────────────────────────────────────────────────

class EasyMasterProcessor : public juce::AudioProcessor
{
public:
    EasyMasterProcessor();
    ~EasyMasterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    ProcessingEngine& getEngine() { return engine; }
    PresetManager& getPresetManager() { return presetManager; }
    LicenseManager& getLicenseManager() { return licenseManager; }

private:
    ProcessingEngine engine;
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;
    LicenseManager licenseManager;
    float smoothedMatchGain = 1.0f;
    float smoothedInputLoudness = -100.0f;
    float smoothedOutputLoudness = -100.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyMasterProcessor)
};

// ─────────────────────────────────────────────────────────────
//  PLUGIN EDITOR
// ─────────────────────────────────────────────────────────────

class EasyMasterEditor : public juce::AudioProcessorEditor,
                          public juce::Timer
{
public:
    explicit EasyMasterEditor (EasyMasterProcessor&);
    ~EasyMasterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void showStage (int stage);
    void refreshTabLabels();
    void updateSatModeVisibility();
    void layoutSatMultiband (juce::Rectangle<int> panelArea);
    float freqToX (float freq, float x, float w) const;
    float xToFreq (float xPos, float x, float w) const;

    EasyMasterProcessor& processor;
    int currentStage = 0;

    // Stage order mapping: stageTypeForTab[tabIndex] = stageType
    // Tab 0 = INPUT (always 0), Tabs 1-7 = reorderable, Tab 8 = LIMITER (always 8)
    std::array<int, 9> stageTypeForTab { {0, 1, 2, 3, 4, 5, 6, 7, 8} };

    // Top bar
    juce::ComboBox presetSelector;
    juce::TextButton savePresetButton { "SAVE" }, deletePresetButton { "DEL" }, initButton { "RESET" };
    juce::TextButton globalBypassButton { "BYPASS" };
    juce::TextButton autoMatchButton { "MATCH" };
    juce::Label lufsLabel, truePeakLabel;

    // Stage tabs
    juce::OwnedArray<juce::TextButton> tabButtons;

    // Reorder buttons
    juce::TextButton moveLeftBtn { juce::String::charToString (0x25C0) };
    juce::TextButton moveRightBtn { juce::String::charToString (0x25B6) };

    // Per-stage bypass toggles (mapped to existing On params, skipping INPUT)
    juce::OwnedArray<juce::ToggleButton> stageBypassToggles;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachments;

    // All knobs: sliders + labels + attachments, tracked by stage
    juce::OwnedArray<juce::Slider> allSliders;
    juce::OwnedArray<juce::Label> allLabels;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> allAttachments;
    juce::Array<int> stageForControl;

    // All combos
    juce::OwnedArray<juce::ComboBox> allCombos;
    juce::OwnedArray<juce::Label> comboLabels;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachments;
    juce::Array<int> comboStage;

    // Inline toggles (HP On, LP On, Auto Release etc.)
    juce::OwnedArray<juce::ToggleButton> inlineToggles;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> inlineToggleAttachments;
    juce::Array<int> toggleStage;

    // Bottom
    juce::Slider masterOutputSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterOutputAttachment;
    juce::ComboBox oversamplingCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment;

    // Bypass attachments for global
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> globalBypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoMatchAttachment;

    // SAT sub-stages: 3=common(Mode), 300=single-only, 301=multiband-only
    static constexpr int kSatCommon = 3;
    static constexpr int kSatSingle = 300;
    static constexpr int kSatMulti  = 301;
    static constexpr int kImager    = 302; // imager width knobs on LIMITER tab

    // FFT crossover dragging
    int draggingXover = -1; // -1=none, 0/1/2 = xover index
    juce::Rectangle<float> fftDisplayArea;

    // Imager solo button hit areas (painted in LIMITER stage)
    std::array<juce::Rectangle<float>, 4> imgSoloBtnRects;

    // Imager crossover dragging
    int draggingImgXover = -1;
    juce::Rectangle<float> imagerDisplayArea;

    // Output EQ node dragging
    int draggingEQNode = -1;  // -1=none, 0-4=band index
    juce::Rectangle<float> eqDisplayArea;
    float eqDbRange = 18.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyMasterEditor)
};
