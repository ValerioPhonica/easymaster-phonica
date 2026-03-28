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
    juce::dsp::LinkwitzRileyFilter<double> crossoverLP, crossoverHP;
    std::atomic<double> inputGain{1.0}, lowWidth{100.0}, highWidth{100.0};
    std::atomic<double> crossoverFreq{300.0}, midGain{1.0}, sideGain{1.0};
    std::atomic<bool> stageOn{true};
    std::atomic<int> crossoverMode{0};
    std::atomic<float> correlation{1.0f};
    juce::AudioBuffer<double> lowBand, highBand;
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

private:
    // EQP-1A + MEQ-5 filters — stereo (L + R)
    juce::dsp::IIR::Filter<double> lowBoostL, lowBoostR, lowAttenL, lowAttenR;
    juce::dsp::IIR::Filter<double> highBoostL, highBoostR, highAttenL, highAttenR;
    juce::dsp::IIR::Filter<double> lowMidL, lowMidR, midDipL, midDipR, highMidL, highMidR;
    std::atomic<bool> stageOn{true};
    std::atomic<float> lowBoostFreq{60}, lowBoostGain{0}, lowAttenFreq{60}, lowAttenGain{0};
    std::atomic<float> highBoostFreq{8000}, highBoostGain{0}, highAttenFreq{8000}, highAttenBW{1};
    std::atomic<float> lowMidFreq{200}, lowMidGain{0}, midDipFreq{1000}, midDipGain{0};
    std::atomic<float> highMidFreq{3000}, highMidGain{0};
    void updateFilters();
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

private:
    juce::dsp::IIR::Filter<double> hsL, hsR, lsL, lsR, midL, midR;
    std::atomic<bool> stageOn{true};
    std::atomic<float> hsFreq{8000}, hsGain{0}, lsFreq{100}, lsGain{0}, midFreq{1000}, midGain{0};
    void updateFilters();
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
//  LUFS METER — ITU-R BS.1770
// ─────────────────────────────────────────────────────────────

class LUFSMeter
{
public:
    LUFSMeter() = default;
    void prepare (double sampleRate, int samplesPerBlock);
    void process (const juce::AudioBuffer<float>& buffer);
    void reset();
    float getIntegratedLUFS() const { return integratedLUFS.load(); }
    float getMomentaryLUFS() const  { return momentaryLUFS.load(); }
    float getTruePeak() const       { return truePeak.load(); }
private:
    juce::dsp::IIR::Filter<float> preFilterL, preFilterR, rlbFilterL, rlbFilterR;
    std::vector<double> blockPowers;
    double momentaryPower = 0.0;
    int momentaryBlockCount = 0;
    std::atomic<float> integratedLUFS{-100.f}, momentaryLUFS{-100.f}, truePeak{-100.f};
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

private:
    std::unique_ptr<InputStage> inputStage;
    std::unique_ptr<LimiterStage> limiterStage;
    std::array<std::unique_ptr<ProcessingStage>, NUM_REORDERABLE> reorderableStages;
    std::array<std::atomic<int>, NUM_REORDERABLE> stageOrder;
    juce::SpinLock orderLock;
    juce::AudioBuffer<double> doubleBuffer;
    std::unique_ptr<OversamplingEngine> oversamplingEngine;
    std::unique_ptr<LUFSMeter> lufsMeter;
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
    juce::TextButton savePresetButton { "Save" }, initButton { "INIT" };
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

    // FFT crossover dragging
    int draggingXover = -1; // -1=none, 0/1/2 = xover index
    juce::Rectangle<float> fftDisplayArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyMasterEditor)
};
