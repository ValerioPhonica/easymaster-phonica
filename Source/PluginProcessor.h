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

private:
    juce::dsp::LinkwitzRileyFilter<double> xover1LP, xover1HP, xover2LP, xover2HP, xover3LP, xover3HP;
    std::array<juce::AudioBuffer<double>, 4> bandBuffers;
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
    double saturateSample (double input, int type, double driveLinear);
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
//  STAGE 6B: DYNAMIC RESONANCE CONTROL
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

private:
    static constexpr int NUM_BANDS = 8;
    struct DynBand {
        juce::dsp::IIR::Filter<double> filterL, filterR;
        double envelope = 0.0;
        double centerFreq = 1000.0;
    };
    std::array<DynBand, NUM_BANDS> bands;
    std::atomic<bool> stageOn{true};
    std::atomic<float> depth{0}, sensitivity{50};
    double attackCoeff = 0.0, releaseCoeff = 0.0;
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

private:
    void showStage (int stage);

    EasyMasterProcessor& processor;
    int currentStage = 0;

    // Top bar
    juce::ComboBox presetSelector;
    juce::TextButton savePresetButton { "Save" }, initButton { "INIT" };
    juce::Label lufsLabel, truePeakLabel;

    // Stage tabs
    juce::OwnedArray<juce::TextButton> tabButtons;

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

    // Bottom
    juce::Slider masterOutputSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterOutputAttachment;
    juce::ComboBox oversamplingCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyMasterEditor)
};
