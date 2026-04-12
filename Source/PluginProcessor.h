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
        OutputEQ, Filter, DynamicEQ, DynamicResonance, Clipper, MultibandDynamics, Limiter,
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
    double getSampleRate() const { return currentSampleRate; }

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

    // M/S processing mode: 0=Stereo, 1=Mid only, 2=Side only
    std::atomic<int> msMode { 0 };

    // Encode L/R → M/S in-place
    static void encodeMS (juce::dsp::AudioBlock<double>& block)
    {
        if (block.getNumChannels() < 2) return;
        auto* l = block.getChannelPointer (0);
        auto* r = block.getChannelPointer (1);
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            double mid  = (l[i] + r[i]) * 0.5;
            double side = (l[i] - r[i]) * 0.5;
            l[i] = mid; r[i] = side;
        }
    }
    // Decode M/S → L/R in-place
    static void decodeMS (juce::dsp::AudioBlock<double>& block)
    {
        if (block.getNumChannels() < 2) return;
        auto* m = block.getChannelPointer (0);
        auto* s = block.getChannelPointer (1);
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            double left  = m[i] + s[i];
            double right = m[i] - s[i];
            m[i] = left; s[i] = right;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessingStage)
};

// ─────────────────────────────────────────────────────────────
//  LINEAR PHASE FIR UTILITY — Converts IIR magnitude response to FIR
// ─────────────────────────────────────────────────────────────

class LinearPhaseFIR
{
public:
    // 4096 taps → 2048 samples latency (~43ms @ 48kHz)
    // Pro-quality linear phase — same class as FabFilter Pro-Q / Ozone
    static constexpr int FIR_SIZE = 4096;
    static constexpr int FFT_SIZE = FIR_SIZE * 2;             // 8192 for overlap-save
    static constexpr int FFT_ORDER_CONV = 13;                  // log2(8192)
    static constexpr int BLOCK_SIZE = FIR_SIZE;                // valid output samples per FFT

    LinearPhaseFIR() = default;
    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::dsp::AudioBlock<double>& block);
    void processMono (double* data, int numSamples);
    void reset();

    void designHighpass (double cutoffHz, double sampleRate, int slopeDb);
    void designLowpass (double cutoffHz, double sampleRate, int slopeDb);

    int getLatency() const { return active ? FIR_SIZE / 2 : 0; }
    bool isActive() const { return active; }

    double getMagnitudeAtFreq (double freq, double sampleRate) const;

private:
    // ─── Overlap-add FFT convolution ───
    struct ChannelConv {
        std::vector<float> inputRing;       // accumulates FIR_SIZE input samples
        int writePos = 0;
        std::vector<float> fftWorkspace;    // FFT workspace, size FFT_SIZE * 2
        std::vector<float> outputQueue;     // circular output FIFO
        int outputReadPos = 0;
        int outputWritePos = 0;
        int outputAvail = 0;
    };
    ChannelConv convL, convR, convMono;

    // Frequency-domain kernel H(k) — precomputed, double-buffered (float for JUCE FFT)
    std::vector<float> kernelFreqA, kernelFreqB;
    std::atomic<int> activeKernel { 0 };
    std::atomic<bool> newKernelReady { false };

    // Time-domain kernel (for getMagnitudeAtFreq display)
    std::vector<double> kernelTime;

    juce::dsp::FFT fftEngine { FFT_ORDER_CONV };

    // Fast kernel design FFT (replaces O(N²) cosine synthesis with O(N·logN))
    static constexpr int DESIGN_FFT_ORDER = 12; // log2(4096)
    juce::dsp::FFT designFft { DESIGN_FFT_ORDER };

    bool active = false;
    bool prepared = false;

    void processChannel (ChannelConv& ch, double* inOut, int numSamples);

    static double besselI0 (double x);
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
    std::atomic<float> balance{0};
    std::atomic<bool> stageOn{true}, dcFilter{false}, phaseInvertL{false}, phaseInvertR{false}, monoCheck{false};
    std::atomic<float> correlation{1.0f};
    // DC blocking filter (HP ~5Hz)
    juce::dsp::IIR::Filter<double> dcBlockL, dcBlockR;

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
    double getMagnitudeAtFreqMid (double freq) const;
    double getMagnitudeAtFreqSide (double freq) const;

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

    // ─── M/S dual: independent Mid filters (13 mono) ───
    juce::dsp::IIR::Filter<double> midLowShelf, midLowResonance, midLowAtten;
    juce::dsp::IIR::Filter<double> midHighPeak, midHighAir, midHighAtten;
    juce::dsp::IIR::Filter<double> midLowMid, midLowMidSkirt;
    juce::dsp::IIR::Filter<double> midMidDip, midMidDipSkirt;
    juce::dsp::IIR::Filter<double> midHighMid, midHighMidSkirt;
    juce::dsp::IIR::Filter<double> midXfmr;

    // ─── M/S dual: independent Side filters (13 mono) ───
    juce::dsp::IIR::Filter<double> sideLowShelf, sideLowResonance, sideLowAtten;
    juce::dsp::IIR::Filter<double> sideHighPeak, sideHighAir, sideHighAtten;
    juce::dsp::IIR::Filter<double> sideLowMid, sideLowMidSkirt;
    juce::dsp::IIR::Filter<double> sideMidDip, sideMidDipSkirt;
    juce::dsp::IIR::Filter<double> sideHighMid, sideHighMidSkirt;
    juce::dsp::IIR::Filter<double> sideXfmr;

    // Parameters
    std::atomic<bool> stageOn{true};
    std::atomic<float> lowBoostFreq{60}, lowBoostGain{0}, lowAttenGain{0};
    std::atomic<float> highBoostFreq{3000}, highBoostGain{0}, highAttenGain{0}, highAttenFreq{10000}, highAttenBW{5};
    std::atomic<float> lowMidFreq{200}, lowMidGain{0}, midDipFreq{1000}, midDipGain{0};
    std::atomic<float> highMidFreq{1500}, highMidGain{0};

    // Mid params (M/S mode)
    std::atomic<float> mLowBoostFreq{60}, mLowBoostGain{0}, mLowAttenGain{0};
    std::atomic<float> mHighBoostFreq{3000}, mHighBoostGain{0}, mHighAttenGain{0}, mHighAttenFreq{10000}, mHighAttenBW{5};
    std::atomic<float> mLowMidFreq{200}, mLowMidGain{0}, mMidDipFreq{1000}, mMidDipGain{0};
    std::atomic<float> mHighMidFreq{1500}, mHighMidGain{0};

    // Side params (M/S mode)
    std::atomic<float> sLowBoostFreq{60}, sLowBoostGain{0}, sLowAttenGain{0};
    std::atomic<float> sHighBoostFreq{3000}, sHighBoostGain{0}, sHighAttenGain{0}, sHighAttenFreq{10000}, sHighAttenBW{5};
    std::atomic<float> sLowMidFreq{200}, sLowMidGain{0}, sMidDipFreq{1000}, sMidDipGain{0};
    std::atomic<float> sHighMidFreq{1500}, sHighMidGain{0};

    void updateFilters();
    void updateMidFilters();
    void updateSideFilters();

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

    // GR + input waveform history for Pro-C style display
    static constexpr int GR_HISTORY_SIZE = 512;
    const std::array<float, GR_HISTORY_SIZE>& getGRHistory() const { return grHistory; }
    const std::array<float, GR_HISTORY_SIZE>& getInputHistory() const { return inputHistory; }
    int getGRHistoryPos() const { return grHistoryPos.load (std::memory_order_relaxed); }

private:
    // ─── Shared state ───
    double envelope = 0.0;
    double attackCoeff = 0.0, releaseCoeff = 0.0;
    juce::dsp::IIR::Filter<double> scHpL, scHpR;
    std::atomic<bool> stageOn{true}, autoRelease{false};
    std::atomic<int> model{0};
    std::atomic<float> threshold{-20}, ratio{4}, attackMs{10}, releaseMs{100};
    std::atomic<float> makeupGain{0}, mix{100}, scHpFreq{20};
    void updateCoefficients();

    // ─── Model-specific state ───

    // Opto (LA-2A): dual-stage release envelope
    double optoGR = 0.0;          // optical cell gain reduction (slow)
    double optoFastGR = 0.0;      // fast attack GR
    double optoCellHistory = 0.0; // T4B cell memory (program-dependent release)

    // FET (1176): feedback topology state + harmonics
    double fetGR = 0.0;           // FET attenuation state
    double fetFeedbackEnv = 0.0;  // feedback envelope

    // Vari-Mu (Fairchild): tube bias state
    double variMuBias = 0.0;      // tube bias voltage (IS the gain reduction)
    double variMuEnv = 0.0;       // slow-responding tube envelope

    // ─── Per-model processing ───
    double processVCA (double peakDb);
    double processOpto (double rmsDb);
    double processFET (double peakDb);
    double processVariMu (double rmsDb);

    // ─── Model-specific saturation ───
    double fetSaturate (double x, double grAmount) const;
    double variMuSaturate (double x) const;

    std::array<float, GR_HISTORY_SIZE> grHistory {};
    std::array<float, GR_HISTORY_SIZE> inputHistory {};
    std::atomic<int> grHistoryPos { 0 };

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
    int getLatencySamples() const override;

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
    // Mid single-band params (M/S mode)
    std::atomic<int> mSatType{0};
    std::atomic<float> mDrive{0}, mBits{16}, mRate{44100}, mOutput{0}, mBlend{100};
    // Side single-band params (M/S mode)
    std::atomic<int> sSatType{0};
    std::atomic<float> sDrive{0}, sBits{16}, sRate{44100}, sOutput{0}, sBlend{100};
    std::atomic<float> xoverFreq1{120}, xoverFreq2{1000}, xoverFreq3{5000};
    std::atomic<int> xoverMode{0};

    // Linear phase crossover: 3 LP FIR filters applied in PARALLEL to input
    // band0 = LP1(input)
    // band1 = LP2(input) - LP1(input)
    // band2 = LP3(input) - LP2(input)
    // band3 = input_delayed - LP3(input)
    // All at same latency (FIR_SIZE/2) → perfect reconstruction
    LinearPhaseFIR linXoverLP1, linXoverLP2, linXoverLP3;
    bool linXoverBuilt = false;
    float lastXF1 = -1, lastXF2 = -1, lastXF3 = -1;
    void rebuildLinearPhaseCrossover();
    static constexpr int LP_DELAY = LinearPhaseFIR::FIR_SIZE / 2;
    // Delay line for raw input (to match FIR latency for band3 = input_del - LP3)
    juce::AudioBuffer<double> inputDelayBuf;
    int inputDelayWP = 0;
    // Extra buffers for parallel LP outputs
    juce::AudioBuffer<double> lp1Buf, lp2Buf, lp3Buf;
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
    // M/S mode: separate SR hold per M/S channel
    double midSRHold = 0, midSRCounter = 0;
    double sideSRHold = 0, sideSRCounter = 0;

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
    double getMagnitudeAtFreqMid (double freq) const;
    double getMagnitudeAtFreqSide (double freq) const;

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
    BandInfo getBandInfoMid (int band) const;
    BandInfo getBandInfoSide (int band) const;

private:
    // 5 bands: Low Shelf, Low-Mid Peak, Mid Peak, High-Mid Peak, High Shelf
    // Stereo EQ filters
    juce::dsp::IIR::Filter<double> bandL[NUM_BANDS], bandR[NUM_BANDS];
    // Mid channel EQ
    juce::dsp::IIR::Filter<double> midBandL[NUM_BANDS];
    // Side channel EQ
    juce::dsp::IIR::Filter<double> sideBandL[NUM_BANDS], sideBandR[NUM_BANDS];
    std::atomic<bool> stageOn{true};

    // Stereo params
    std::atomic<float> freq[NUM_BANDS], gain[NUM_BANDS], q[NUM_BANDS];
    // Mid params
    std::atomic<float> midFreq[NUM_BANDS], midGain[NUM_BANDS], midQ[NUM_BANDS];
    // Side params
    std::atomic<float> sideFreq[NUM_BANDS], sideGain[NUM_BANDS], sideQ[NUM_BANDS];

    void updateFilters();
    void updateMidFilters();
    void updateSideFilters();

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

    // FFT for spectrum analyzer
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    void pushSampleToFFT (float sample);
    bool isFFTReady() const { return fftReady.load (std::memory_order_acquire); }
    void computeFFTMagnitudes();
    const std::array<float, fftSize / 2>& getMagnitudes() const { return fftMagnitudes; }

private:
    static constexpr int MAX_STAGES = 4;
    // Stereo filters
    std::array<juce::dsp::IIR::Filter<double>, MAX_STAGES> hpL, hpR, lpL, lpR;
    // Mid filters (mono, for M/S mode)
    std::array<juce::dsp::IIR::Filter<double>, MAX_STAGES> hpMid, lpMid;
    // Side filters (mono, for M/S mode)
    std::array<juce::dsp::IIR::Filter<double>, MAX_STAGES> hpSide, lpSide;

    std::atomic<bool> stageOn{true}, hpOn{false}, lpOn{false};
    std::atomic<float> hpFreq{30}, lpFreq{18000};
    std::atomic<int> hpSlope{1}, lpSlope{1}, filterMode{0};
    // Mid params
    std::atomic<bool> mHpOn{false}, mLpOn{false};
    std::atomic<float> mHpFreq{30}, mLpFreq{18000};
    std::atomic<int> mHpSlope{1}, mLpSlope{1};
    // Side params
    std::atomic<bool> sHpOn{false}, sLpOn{false};
    std::atomic<float> sHpFreq{30}, sLpFreq{18000};
    std::atomic<int> sHpSlope{1}, sLpSlope{1};

    void updateFilters();
    void updateMidFilters();
    void updateSideFilters();
    void rebuildLinearPhase();

    // Linear phase FIR (stereo)
    LinearPhaseFIR linPhaseHP, linPhaseLP;
    // Linear phase FIR (M/S)
    LinearPhaseFIR linPhaseHPMid, linPhaseLPMid;
    LinearPhaseFIR linPhaseHPSide, linPhaseLPSide;
    float lastHPFreq = -1, lastLPFreq = -1;
    int lastHPSlope = -1, lastLPSlope = -1;
    bool lastHPOn = false, lastLPOn = false;
    bool linPhaseBuilt = false;

    // M/S latency compensation delay lines
    static constexpr int MAX_LINPHASE_DELAY = LinearPhaseFIR::FIR_SIZE; // max 2 FIRs in series
    std::vector<double> msDelayMid, msDelaySide;
    int msDelayMidWP = 0, msDelaySideWP = 0;

    // FFT internals
    juce::dsp::FFT fftProcessor { fftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> fftFifo {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize / 2> fftMagnitudes {};
    int fftFifoIndex = 0;
    std::atomic<bool> fftReady { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterStage)
};

// ─────────────────────────────────────────────────────────────
//  STAGE 6C: DYNAMIC EQ — 5-band parametric with per-band dynamics
// ─────────────────────────────────────────────────────────────

class DynamicEQStage : public ProcessingStage
{
public:
    DynamicEQStage() : ProcessingStage (StageID::DynamicEQ, "Dynamic EQ", true) {}
    void prepare (double sr, int bs) override;
    void process (juce::dsp::AudioBlock<double>& block) override;
    void reset() override;
    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& apvts) override;

    // EQ curve for display
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
    struct BandInfo { float freq; float gain; float q; int type; bool on; };
    BandInfo getBandInfo (int band) const;

    // Dynamic params for UI popup
    struct DynInfo { float threshold; float range; float ratio; float attack; float release; };
    DynInfo getDynInfo (int band) const;

    // Per-band GR for UI display (dB, negative = cutting)
    std::array<std::atomic<float>, NUM_BANDS> bandGRDisplay {};

    // Current dynamic gain applied per band (for curve display)
    std::array<std::atomic<float>, NUM_BANDS> dynamicGainDb {};

private:
    // 5 EQ bands: Low Shelf, Low-Mid Peak, Mid Peak, High-Mid Peak, High Shelf
    juce::dsp::IIR::Filter<double> bandL[NUM_BANDS], bandR[NUM_BANDS];

    // Sidechain bandpass filters (2nd order) for envelope detection
    juce::dsp::IIR::Filter<double> scBpL[NUM_BANDS], scBpR[NUM_BANDS];

    std::atomic<bool> stageOn { true };
    // Per-band static EQ params
    std::atomic<float> freq[NUM_BANDS], gain[NUM_BANDS], q[NUM_BANDS];
    std::atomic<bool> bandOn[NUM_BANDS];
    // Per-band dynamic params
    std::atomic<float> dynThreshold[NUM_BANDS];
    std::atomic<float> dynRange[NUM_BANDS];
    std::atomic<float> dynRatio[NUM_BANDS];
    std::atomic<float> dynAttack[NUM_BANDS];
    std::atomic<float> dynRelease[NUM_BANDS];

    // Envelope state per band (L+R summed)
    double envState[NUM_BANDS] {};
    double smoothedDynGain[NUM_BANDS] {}; // current dynamic gain in linear

    void updateFilters();
    void updateScFilters();

    // FFT
    juce::dsp::FFT fftProcessor { fftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> fftFifo {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize / 2> fftMagnitudes {};
    int fftFifoIndex = 0;
    std::atomic<bool> fftReady { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamicEQStage)
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
    static constexpr int NUM_BANDS = 32;
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
    std::atomic<float> midFreq      { 2000.0f };
    std::atomic<float> highFreq     { 12000.0f };
    std::atomic<float> lowDepth     { 100.0f };  // % of main depth
    std::atomic<float> midDepth     { 100.0f };
    std::atomic<float> highDepth    { 100.0f };
    std::atomic<int>   dynMode      { 0 }; // 0=Soft (-6dB max), 1=Hard (-12dB max)

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

    // Transfer function for UI
    float getTransferCurve (float inputDb);

    // Waveform access for UI
    int getWaveWritePos() const { return waveWritePos.load(); }
    float getWaveIn (int idx) const { return waveIn[(size_t)((idx + WAVE_BUF_SIZE) % WAVE_BUF_SIZE)]; }
    float getWaveOut (int idx) const { return waveOut[(size_t)((idx + WAVE_BUF_SIZE) % WAVE_BUF_SIZE)]; }
    float getInputPeakDb() const { return inputPeakDb.load(); }
    float getClipAmountDb() const { return clipAmountDb.load(); }

    // Waveform ring buffer for UI display
    static constexpr int WAVE_BUF_SIZE = 2048;
    std::array<float, WAVE_BUF_SIZE> waveIn {}, waveOut {};
    std::atomic<int> waveWritePos { 0 };

    // Peak meters
    std::atomic<float> inputPeakDb { -100.0f };
    std::atomic<float> outputPeakDb { -100.0f };
    std::atomic<float> clipAmountDb { 0.0f };  // how much is being clipped

    // Clip amount history for scrolling display
    static constexpr int CLIP_HISTORY_SIZE = 512;
    const std::array<float, CLIP_HISTORY_SIZE>& getClipHistory() const { return clipHistory; }
    int getClipHistoryPos() const { return clipHistoryPos.load (std::memory_order_relaxed); }

private:
    std::atomic<bool> stageOn{true};
    std::atomic<float> inputGain{0}, ceiling{-0.3f}, shape{50}, transient{0}, outputGain{0}, mixPct{100};
    std::atomic<int> clipMode{0};

    // Transient detection — dual envelope (fast peak vs slow peak)
    double fastEnvL = 0, fastEnvR = 0;   // fast peak follower (~0.1ms attack)
    double slowEnvL = 0, slowEnvR = 0;   // slow peak follower (~50ms attack)
    double fastAttack = 0, fastRelease = 0;
    double slowAttack = 0, slowRelease = 0;

    // Oversampling (internal 2x for anti-aliasing)
    std::unique_ptr<juce::dsp::Oversampling<double>> clipOS;
    bool osReady = false;

    // Airwindows ClipOnly state (slew-limited clipping)
    double lastClipL = 0.0, lastClipR = 0.0;

    // Clip amount history storage
    std::array<float, CLIP_HISTORY_SIZE> clipHistory {};
    std::atomic<int> clipHistoryPos { 0 };

    double clipSample (double input, double ceilLin, double shapeFactor, int mode);
    double clipOnly3Sample (double input, double ceilLin, double& lastSample, int iterations);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipperStage)
};

// ─────────────────────────────────────────────────────────────
//  STAGE 8: MULTIBAND DYNAMICS — Pro-MB style
//  Compress/Expand × Upward/Downward per band
// ─────────────────────────────────────────────────────────────

class MultibandDynamicsStage : public ProcessingStage
{
public:
    MultibandDynamicsStage() : ProcessingStage (StageID::MultibandDynamics, "MB Dynamics", true) {}
    void prepare (double sr, int bs) override;
    void process (juce::dsp::AudioBlock<double>& block) override;
    void reset() override;
    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout) override;
    void updateParameters (const juce::AudioProcessorValueTreeState& apvts) override;
    int getLatencySamples() const override;

    static constexpr int NUM_BANDS = 4;

    // Per-band GR for UI display (dB, negative = cutting)
    std::array<std::atomic<float>, NUM_BANDS> bandGRDisplay {};

private:
    // Crossover filters (IIR Linkwitz-Riley)
    juce::dsp::LinkwitzRileyFilter<double> xover1LP, xover1HP, xover2LP, xover2HP, xover3LP, xover3HP;
    std::array<juce::AudioBuffer<double>, NUM_BANDS> bandBuffers;
    juce::AudioBuffer<double> tempBuffer;

    // Linear phase crossover
    LinearPhaseFIR linXoverLP1, linXoverLP2, linXoverLP3;
    bool linXoverBuilt = false;
    float lastXF1 = -1, lastXF2 = -1, lastXF3 = -1;
    static constexpr int LP_DELAY = LinearPhaseFIR::FIR_SIZE / 2;
    juce::AudioBuffer<double> inputDelayBuf;
    int inputDelayWP = 0;
    juce::AudioBuffer<double> lp1Buf, lp2Buf, lp3Buf;
    void rebuildLinearPhaseCrossover();

    // Global params
    std::atomic<bool> stageOn { true };
    std::atomic<int> msMode { 0 };     // 0=Stereo, 1=Mid, 2=Side
    std::atomic<int> xoverMode { 0 };  // 0=MinPhase, 1=LinearPhase
    std::atomic<float> xoverFreq1 { 120 }, xoverFreq2 { 1000 }, xoverFreq3 { 5000 };
    std::atomic<float> globalMix { 100 }; // 0-200%

    // Per-band params
    struct BandParams
    {
        std::atomic<int> mode { 0 };       // 0=Compress, 1=Expand
        std::atomic<float> threshold { -20 };
        std::atomic<float> range { -24 };   // neg=downward, pos=upward
        std::atomic<float> ratio { 4 };
        std::atomic<float> attack { 10 };
        std::atomic<float> release { 100 };
        std::atomic<float> knee { 6 };
        std::atomic<float> outputGain { 0 };
        std::atomic<bool> solo { false };
        std::atomic<bool> bypass { false };
        std::atomic<float> bandMix { 100 }; // per-band dry/wet 0-100%
    };
    std::array<BandParams, NUM_BANDS> bandParams;

    // Per-band envelope state
    struct BandState
    {
        double envelope = 0.0;
        double gainSmoothed = 1.0;
    };
    std::array<BandState, NUM_BANDS> bandStateL, bandStateR;

    // Dynamics gain computer
    double computeGainDb (double inputDb, double threshDb, double ratioVal,
                          double kneeDb, double rangeDb, int dynMode) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandDynamicsStage)
};

// ─────────────────────────────────────────────────────────────
//  STAGE 9: TRUE PEAK LIMITER — Lookahead + ISP
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
    std::atomic<bool> stageOn{true}, autoRelease{true}, truePeakOn{true};
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

    // Output FFT (mono mix + Mid/Side)
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder; // 2048
    bool isFFTReady() const { return fftReady.load (std::memory_order_acquire); }
    void computeFFTMagnitudes();
    const std::array<float, fftSize / 2>& getMagnitudes() const { return magnitudes; }
    const std::array<float, fftSize / 2>& getMidMagnitudes() const { return midMagnitudes; }
    const std::array<float, fftSize / 2>& getSideMagnitudes() const { return sideMagnitudes; }

    // Analyzer speed: 0=Slow, 1=Medium, 2=Fast
    void setAnalyzerSpeed (int speed) { analyzerSpeed.store (speed); }
    int getAnalyzerSpeed() const { return analyzerSpeed.load(); }

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

    // FFT (mono mix)
    juce::dsp::FFT fftProcessor { fftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> fifo {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize / 2> magnitudes {};
    int fifoIndex = 0;
    std::atomic<bool> fftReady { false };
    // Mid/Side FFT
    std::array<float, fftSize> fifoL {}, fifoR {};
    std::array<float, fftSize * 2> msFftData {};
    std::array<float, fftSize / 2> midMagnitudes {}, sideMagnitudes {};
    int msfifoIndex = 0;
    std::atomic<int> analyzerSpeed { 1 }; // 0=Slow, 1=Medium, 2=Fast

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

    static constexpr int NUM_REORDERABLE = 9;

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
    std::atomic<int> ditherMode{0}; // 0=Off, 1=TPDF 16, 2=TPDF 24, 3=NJAD 16, 4=NJAD 24, 5=Dark 16, 6=Dark 24
    juce::Random ditherRng;
    // NJAD (Not Just Another Dither) — error feedback state
    double njadErrorL = 0.0, njadErrorR = 0.0;
    // Dark dither — noise-shaped error feedback (less HF noise)
    double darkErrorL[3] = {}, darkErrorR[3] = {};
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
    void setStageOrder (const std::array<int, 9>& order) { stageOrder = order; }
    std::array<int, 9> getStageOrder() const { return stageOrder; }
private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentPreset { "INIT" };
    std::array<int, 9> stageOrder { 0,1,2,3,4,5,6,8,7 };
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

    // ─── Reference Track System ───
    void loadReferenceFile (const juce::File& file);
    void clearReference();
    bool hasReference() const { return refLoaded.load(); }
    bool isABActive() const { return abActive.load(); }
    void setABActive (bool active) { abActive.store (active); }
    juce::String getRefFileName() const { return refFileName; }

    // Waveform display data
    static constexpr int WAVEFORM_POINTS = 800;
    const std::array<float, WAVEFORM_POINTS>& getRefWaveformPeaks() const { return refWaveformPeaks; }
    float getRefDurationSeconds() const;
    float getRefPlayPositionNorm() const; // 0..1
    void setRefPlayPosition (float normPos); // click-to-seek

    // ─── Pro Spectrum Analyzer (8192-point FFT, Mid/Side) ───
    static constexpr int REF_FFT_ORDER = 13;
    static constexpr int REF_FFT_SIZE = 1 << REF_FFT_ORDER; // 8192
    static constexpr int REF_FFT_HALF = REF_FFT_SIZE / 2;   // 4096 bins

    // Master spectrum: Mid + Side (in dB, -100..0)
    const std::array<float, REF_FFT_HALF>& getMasterMidSpectrum()  const { return masterMidMags; }
    const std::array<float, REF_FFT_HALF>& getMasterSideSpectrum() const { return masterSideMags; }
    // Reference spectrum: Mid + Side
    const std::array<float, REF_FFT_HALF>& getRefMidSpectrum()  const { return refMidMags; }
    const std::array<float, REF_FFT_HALF>& getRefSideSpectrum() const { return refSideMags; }
    // Legacy compatibility
    const std::array<float, REF_FFT_HALF>& getMasterSpectrum() const { return masterMidMags; }
    const std::array<float, REF_FFT_HALF>& getRefSpectrum()    const { return refMidMags; }
    bool isRefSpectrumReady() const { return refSpecReady.load(); }

private:
    ProcessingEngine engine;
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;
    LicenseManager licenseManager;
    float smoothedMatchGain = 1.0f;
    float smoothedInputLoudness = -100.0f;
    float smoothedOutputLoudness = -100.0f;
    float smoothedRefMatchGain = 1.0f;
    std::atomic<int> analyzerSpeed { 1 };

    // Reference track
    juce::AudioBuffer<float> refBuffer;
    std::atomic<bool> refLoaded { false };
    std::atomic<bool> abActive { false };
    std::atomic<int64_t> refPlayPos { 0 };
    float refLufs = -100.0f;
    juce::String refFileName;
    juce::AudioFormatManager refFormatManager;
    std::array<float, WAVEFORM_POINTS> refWaveformPeaks {};

    // Pro spectrum analysis (8192-point, Mid/Side)
    juce::dsp::FFT refFft { REF_FFT_ORDER };
    juce::dsp::WindowingFunction<float> refWindow { (size_t) REF_FFT_SIZE, juce::dsp::WindowingFunction<float>::hann };
    // Master: separate L/R FIFOs → compute Mid/Side
    std::array<float, REF_FFT_SIZE> masterFifoL {}, masterFifoR {};
    std::array<float, REF_FFT_SIZE * 2> masterFftWork {};
    std::array<float, REF_FFT_HALF> masterMidMags {}, masterSideMags {};
    int masterFifoPos = 0;
    // Reference: separate L/R FIFOs
    std::array<float, REF_FFT_SIZE> refFifoL {}, refFifoR {};
    std::array<float, REF_FFT_SIZE * 2> refFftWork {};
    std::array<float, REF_FFT_HALF> refMidMags {}, refSideMags {};
    int refFifoPos = 0;
    std::atomic<bool> refSpecReady { false };
    void pushToMasterFFT (float sampleL, float sampleR);
    void pushToRefFFT (float sampleL, float sampleR);
    void computeSpectrum (const float* fifoL, const float* fifoR,
                          std::array<float, REF_FFT_HALF>& midMags,
                          std::array<float, REF_FFT_HALF>& sideMags);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyMasterProcessor)
};

// ─────────────────────────────────────────────────────────────
//  CUSTOM LOOK & FEEL — Professional mastering plugin aesthetics
// ─────────────────────────────────────────────────────────────

class EasyMasterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EasyMasterLookAndFeel()
    {
        // Dark theme colors
        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFFE94560));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xFF2A2A50));
        setColour (juce::Slider::thumbColourId, juce::Colour (0xFF55DDEE));
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xFF1A1A38));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xFF3A3A60));
        setColour (juce::ComboBox::textColourId, juce::Colour (0xFFCCCCDD));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xFF1A1A38));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xFFCCCCDD));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xFF3A3A60));
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF1E1E3A));
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::TextButton::textColourOffId, juce::Colour (0xFFAABBCC));
        setColour (juce::ToggleButton::textColourId, juce::Colour (0xFFAABBCC));
        setColour (juce::ToggleButton::tickColourId, juce::Colour (0xFF55DDEE));
        setColour (juce::Label::textColourId, juce::Colour (0xFF99AABB));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        float radius = (float) juce::jmin (width, height) * 0.38f;
        float centreX = (float) x + (float) width * 0.5f;
        float centreY = (float) y + (float) height * 0.5f;
        float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // ─── Background ring ───
        float ringW = radius * 0.18f;
        g.setColour (juce::Colour (0xFF1A1A35));
        g.drawEllipse (centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, ringW);

        // ─── Track arc (dark) ───
        juce::Path trackArc;
        trackArc.addCentredArc (centreX, centreY, radius, radius, 0,
                                rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour (0xFF252545));
        g.strokePath (trackArc, juce::PathStrokeType (ringW, juce::PathStrokeType::curved,
                      juce::PathStrokeType::rounded));

        // ─── Value arc (colored) ───
        if (sliderPos > 0.001f)
        {
            juce::Path valueArc;
            // Bi-directional for bipolar params (centered at 0.5)
            bool isBipolar = slider.getMinimum() < 0 && slider.getMaximum() > 0;
            float startAng = isBipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;
            float endAng = angle;
            if (isBipolar && endAng < startAng) std::swap (startAng, endAng);

            valueArc.addCentredArc (centreX, centreY, radius, radius, 0,
                                    startAng, endAng, true);

            // Gradient: pink to cyan based on position
            juce::Colour arcCol = juce::Colour (0xFFE94560).interpolatedWith (
                juce::Colour (0xFF55DDEE), sliderPos);
            g.setColour (arcCol);
            g.strokePath (valueArc, juce::PathStrokeType (ringW, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded));
        }

        // ─── Knob body (subtle gradient) ───
        float innerR = radius * 0.65f;
        juce::ColourGradient bodyGrad (juce::Colour (0xFF2A2A48), centreX, centreY - innerR,
                                       juce::Colour (0xFF1A1A30), centreX, centreY + innerR, false);
        g.setGradientFill (bodyGrad);
        g.fillEllipse (centreX - innerR, centreY - innerR, innerR * 2.0f, innerR * 2.0f);

        // ─── Pointer line ───
        float pointerLen = innerR * 0.85f;
        float px = centreX + pointerLen * std::sin (angle);
        float py = centreY - pointerLen * std::cos (angle);
        g.setColour (juce::Colour (0xFFCCDDEE));
        g.drawLine (centreX + (innerR * 0.2f) * std::sin (angle),
                    centreY - (innerR * 0.2f) * std::cos (angle),
                    px, py, 2.0f);

        // ─── Value text inside knob ───
        g.setColour (juce::Colour (0xFFDDEEFF));
        g.setFont (juce::Font (juce::jmax (9.0f, radius * 0.32f)));
        auto val = slider.getValue();
        juce::String txt;
        if (std::abs (val) >= 1000) txt = juce::String ((int) val);
        else if (std::abs (val) >= 10) txt = juce::String (val, 1);
        else txt = juce::String (val, 2);
        g.drawText (txt, (int)(centreX - innerR), (int)(centreY - 6), (int)(innerR * 2), 12,
                    juce::Justification::centred);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& bgCol, bool isHighlighted, bool isDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

        // ─── Tab buttons (radio group) — Ozone-style with accent line ───
        if (button.getRadioGroupId() == 1001)
        {
            bool isOn = button.getToggleState();

            // Background
            g.setColour (isOn ? juce::Colour (0xFF1E2848) : juce::Colour (0xFF141430));
            if (isHighlighted && !isOn) g.setColour (juce::Colour (0xFF1A1A3A));
            g.fillRoundedRectangle (bounds.withTrimmedBottom (3), 5.0f);

            // Accent line at bottom when active
            if (isOn)
            {
                g.setColour (juce::Colour (0xFFE94560));
                g.fillRoundedRectangle (bounds.getX() + 4, bounds.getBottom() - 3,
                                        bounds.getWidth() - 8, 3.0f, 1.5f);
            }

            // Text
            g.setColour (isOn ? juce::Colours::white : juce::Colour (0xFF778899));
            g.setFont (juce::Font (11.0f, isOn ? juce::Font::bold : juce::Font::plain));
            g.drawText (button.getButtonText(), bounds.toNearestInt(), juce::Justification::centred);
            return;
        }

        // ─── Regular buttons ───
        auto col = bgCol;
        if (isHighlighted) col = col.brighter (0.1f);
        if (isDown) col = col.brighter (0.2f);
        if (button.getToggleState()) col = col.brighter (0.15f);

        g.setColour (col);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (col.brighter (0.3f).withAlpha (0.3f));
        g.drawRoundedRectangle (bounds, 4.0f, 0.8f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool isHighlighted, bool isDown) override
    {
        // Tab buttons already draw text in drawButtonBackground — skip here
        if (button.getRadioGroupId() == 1001) return;
        // Default for other buttons
        juce::LookAndFeel_V4::drawButtonText (g, button, isHighlighted, isDown);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool isDown,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 0.8f);

        // Arrow
        float arrowX = (float) width - 18.0f;
        float arrowY = (float) height * 0.5f - 2.0f;
        juce::Path arrow;
        arrow.addTriangle (arrowX, arrowY, arrowX + 8.0f, arrowY, arrowX + 4.0f, arrowY + 5.0f);
        g.setColour (juce::Colour (0xFF888899));
        g.fillPath (arrow);
    }

    // No drawLabel override — use default JUCE rendering with proper label setup

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool isHighlighted, bool isDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        bool isOn = button.getToggleState();

        // Checkbox area
        float boxSize = 14.0f;
        float boxX = bounds.getX() + 4.0f;
        float boxY = bounds.getCentreY() - boxSize * 0.5f;

        g.setColour (juce::Colour (0xFF1A1A38));
        g.fillRoundedRectangle (boxX, boxY, boxSize, boxSize, 3.0f);
        g.setColour (juce::Colour (0xFF3A3A60));
        g.drawRoundedRectangle (boxX, boxY, boxSize, boxSize, 3.0f, 0.8f);

        if (isOn)
        {
            g.setColour (juce::Colour (0xFF55DDEE));
            g.fillRoundedRectangle (boxX + 2, boxY + 2, boxSize - 4, boxSize - 4, 2.0f);
        }

        // Label text
        g.setColour (isOn ? juce::Colour (0xFFCCDDEE) : juce::Colour (0xFF778899));
        g.setFont (juce::Font (11.0f));
        g.drawText (button.getButtonText(), (int)(boxX + boxSize + 6), (int) bounds.getY(),
                    (int)(bounds.getWidth() - boxSize - 12), (int) bounds.getHeight(),
                    juce::Justification::centredLeft);
    }
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
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void showStage (int stage);
    void refreshTabLabels();
    void updateSatModeVisibility();
    void layoutSatMultiband (juce::Rectangle<int> panelArea);
    void layoutMBDynBands (juce::Rectangle<int> panelArea);
    float freqToX (float freq, float x, float w) const;
    float xToFreq (float xPos, float x, float w) const;

    EasyMasterProcessor& processor;
    EasyMasterLookAndFeel customLnF;
    int currentStage = 0;

    // Stage order mapping: stageTypeForTab[tabIndex] = stageType
    // Tab 0 = INPUT (always 0), Tabs 1-9 = reorderable, Tab 10 = LIMITER (always 10)
    std::array<int, 11> stageTypeForTab { {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10} };

    // Top bar
    juce::ComboBox presetSelector;
    juce::TextButton savePresetButton { "SAVE" }, deletePresetButton { "DELETE" }, initButton { "RESET" };
    juce::TextButton resetMetersButton { "RST M" };
    juce::TextButton globalBypassButton { "BYPASS" };
    juce::TextButton autoMatchButton { "GAIN MATCH" };

    // Reference track
    juce::TextButton loadRefButton { "LOAD REF" }, abButton { "A/B" };
    juce::Label refNameLabel;
    bool eqEditSide = false;
    juce::TextButton eqMsToggle { "EDIT: MID" };
    int eqKnobStartIdx = -1; // index of first EQ knob in allSliders
    int lastEqMsMode = 0;
    void updateEQKnobAttachments (int mode);

    int pultecKnobStartIdx = -1; // index of first Pultec knob in allSliders
    int pultecComboStartIdx = -1; // index of first swappable Pultec combo
    int lastPultecMsMode = 0;
    void updatePultecKnobAttachments (int mode);

    int satKnobStartIdx = -1; // index of first SAT single-band knob
    int satComboStartIdx = -1; // index of first swappable SAT combo (Type)
    int lastSatMsMode = 0;
    void updateSatKnobAttachments (int mode);

    int filterKnobStartIdx = -1;
    int filterComboStartIdx = -1;
    int filterToggleStartIdx = -1;
    int lastFilterMsMode = 0;
    void updateFilterKnobAttachments (int mode);
    juce::Label lufsLabel, truePeakLabel;

    // Stage tabs
    juce::OwnedArray<juce::TextButton> tabButtons;

    // Reorder buttons
    juce::TextButton moveLeftBtn { "<" };
    juce::TextButton moveRightBtn { ">" };

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
    juce::ComboBox ditherCombo;
    juce::ComboBox analyzerSpeedCombo;
    juce::ToggleButton showRefSpecToggle { "REF" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ditherAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> analyzerSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> showRefSpecAttachment;

    // Bypass attachments for global
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> globalBypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoMatchAttachment;

    // SAT sub-stages: 3=common(Mode), 300=single-only, 301=multiband-only
    static constexpr int kSatCommon = 3;
    static constexpr int kSatSingle = 300;
    static constexpr int kSatMulti  = 301;
    static constexpr int kImager    = 302;
    static constexpr int kMBDynBand = 304;
    static constexpr int kEQSide   = 14;
    static constexpr int kPultecMEQ = 15;

    // FFT crossover dragging
    int draggingXover = -1; // -1=none, 0/1/2 = xover index
    juce::Rectangle<float> fftDisplayArea;

    // Imager solo button hit areas (painted in LIMITER stage)
    std::array<juce::Rectangle<float>, 4> imgSoloBtnRects;

    // Imager crossover dragging
    int draggingImgXover = -1;

    // ─── Imager sliders (4 width knobs + 3 crossover sliders) ───
    std::array<juce::Slider, 4> imgWidthSliders;
    std::array<juce::Slider, 3> imgXoverSliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> imgWidthAttach;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> imgXoverAttach;
    juce::Rectangle<float> imagerDisplayArea;

    // Output EQ node dragging
    int draggingEQNode = -1;  // -1=none, 0-4=band index
    juce::Rectangle<float> eqDisplayArea;
    float eqDbRange = 24.0f;

    // Dynamic EQ node dragging + popup
    int draggingDynEQNode = -1;  // -1=none, 0-4=band index
    juce::Rectangle<float> dynEqDisplayArea;
    int dynEqSelectedBand = -1; // -1=none, 0-4=band with popup
    juce::Rectangle<float> dynEqPopupArea;

    int draggingFilterNode = -1;  // -1=none, 0=HP, 1=LP
    juce::Rectangle<float> filterDisplayArea;
    juce::Rectangle<float> waveformDisplayArea;
    juce::Rectangle<float> mbDynDisplayArea;
    int mbDynDragTarget = -1;  // -1=none, 0-2=xover, 10-13=threshold
    int mbDynSelectedBand = -1; // -1=none, 0-3=band with popup
    std::array<juce::Rectangle<float>, 4> mbDynSoloBtnRects;

    // Inline crossover frequency editor (double-click to type)
    juce::TextEditor xoverFreqEditor;
    juce::String xoverEditParamID;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyMasterEditor)
};
