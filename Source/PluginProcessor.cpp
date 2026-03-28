// ═══════════════════════════════════════════════════════════════
//  EASY MASTER — Phonica School
//  All implementations consolidated
// ═══════════════════════════════════════════════════════════════

#include "PluginProcessor.h"

// ─────────────────────────────────────────────────────────────
//  LINEAR PHASE FIR UTILITY
// ─────────────────────────────────────────────────────────────

void LinearPhaseFIR::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };
    firL.prepare (spec);
    firR.prepare (spec);
    active = false;
    prepared = true;
}

void LinearPhaseFIR::designLowpass (double cutoffHz, double sampleRate)
{
    if (!prepared || sampleRate <= 0 || cutoffHz <= 0) { active = false; return; }

    // Normalized cutoff: 0 to 0.5 (fraction of Nyquist)
    double fc = cutoffHz / sampleRate;
    fc = juce::jlimit (0.0001, 0.4999, fc);

    int M = FIR_SIZE - 1;  // filter order
    int center = M / 2;
    std::vector<double> kernel ((size_t) FIR_SIZE, 0.0);

    // Windowed-sinc lowpass kernel
    for (int i = 0; i < FIR_SIZE; ++i)
    {
        int n = i - center;
        if (n == 0)
            kernel[(size_t) i] = 2.0 * fc;
        else
            kernel[(size_t) i] = std::sin (2.0 * juce::MathConstants<double>::pi * fc * (double) n)
                                 / (juce::MathConstants<double>::pi * (double) n);

        // Kaiser window (beta=6 — good balance between rolloff and transition width)
        double beta = 6.0;
        double x = 2.0 * (double) i / (double) M - 1.0;
        double arg = beta * std::sqrt (std::max (0.0, 1.0 - x * x));
        // Approximate I0(x) with series expansion (10 terms, very accurate)
        double besselI0_arg = 1.0, besselI0_beta = 1.0;
        for (int k = 1; k <= 10; ++k)
        {
            double t_arg = (arg * 0.5) / (double) k;
            besselI0_arg *= t_arg * t_arg;
            double t_beta = (beta * 0.5) / (double) k;
            besselI0_beta *= t_beta * t_beta;
        }
        // Wait, this won't converge. Let me use the proper series:
        // I0(x) = sum_{k=0}^{inf} [(x/2)^k / k!]^2
        besselI0_arg = 1.0; besselI0_beta = 1.0;
        double termA = 1.0, termB = 1.0;
        for (int k = 1; k <= 20; ++k)
        {
            termA *= (arg / 2.0) / (double) k;
            termB *= (beta / 2.0) / (double) k;
            besselI0_arg += termA * termA;
            besselI0_beta += termB * termB;
        }

        double window = besselI0_arg / besselI0_beta;
        kernel[(size_t) i] *= window;
    }

    // Normalize DC gain to 1.0
    double sum = 0;
    for (auto k : kernel) sum += k;
    if (std::abs (sum) > 1e-12)
        for (auto& k : kernel) k /= sum;

    applyKernel (kernel);
}

void LinearPhaseFIR::designHighpass (double cutoffHz, double sampleRate)
{
    if (!prepared || sampleRate <= 0 || cutoffHz <= 0) { active = false; return; }

    // Design as lowpass then spectral inversion
    double fc = cutoffHz / sampleRate;
    fc = juce::jlimit (0.0001, 0.4999, fc);

    int M = FIR_SIZE - 1;
    int center = M / 2;
    std::vector<double> kernel ((size_t) FIR_SIZE, 0.0);

    // Build lowpass kernel first
    for (int i = 0; i < FIR_SIZE; ++i)
    {
        int n = i - center;
        if (n == 0)
            kernel[(size_t) i] = 2.0 * fc;
        else
            kernel[(size_t) i] = std::sin (2.0 * juce::MathConstants<double>::pi * fc * (double) n)
                                 / (juce::MathConstants<double>::pi * (double) n);

        // Kaiser window (beta=6)
        double beta = 6.0;
        double x = 2.0 * (double) i / (double) M - 1.0;
        double arg = beta * std::sqrt (std::max (0.0, 1.0 - x * x));
        double besselI0_arg = 1.0, besselI0_beta = 1.0;
        double termA = 1.0, termB = 1.0;
        for (int k = 1; k <= 20; ++k)
        {
            termA *= (arg / 2.0) / (double) k;
            termB *= (beta / 2.0) / (double) k;
            besselI0_arg += termA * termA;
            besselI0_beta += termB * termB;
        }
        kernel[(size_t) i] *= besselI0_arg / besselI0_beta;
    }

    // Normalize LP
    double sum = 0;
    for (auto k : kernel) sum += k;
    if (std::abs (sum) > 1e-12)
        for (auto& k : kernel) k /= sum;

    // Spectral inversion: negate all, add 1 at center → converts LP to HP
    for (auto& k : kernel) k = -k;
    kernel[(size_t) center] += 1.0;

    applyKernel (kernel);
}

void LinearPhaseFIR::designFromIIRMagnitude (
    const std::vector<juce::dsp::IIR::Coefficients<double>::Ptr>& coeffs, double sampleRate)
{
    if (!prepared || coeffs.empty() || sampleRate <= 0) { active = false; return; }

    // Frequency-sampling method: sample IIR magnitude, IFFT, window, shift
    int N = FIR_SIZE;
    int halfN = N / 2;

    // Build magnitude response
    std::vector<double> mag ((size_t)(halfN + 1));
    for (int i = 0; i <= halfN; ++i)
    {
        double freq = (double) i * sampleRate / (double) N;
        if (freq < 1.0) freq = 1.0;
        double m = 1.0;
        for (auto& c : coeffs)
            if (c) m *= c->getMagnitudeForFrequency (freq, sampleRate);
        mag[(size_t) i] = m;
    }

    // Build symmetric spectrum (zero phase = real only)
    // Use cosine transform approach: h[n] = (1/N) * sum M[k] * cos(2*pi*k*(n-center)/N)
    int center = (N - 1) / 2;
    std::vector<double> kernel ((size_t) N, 0.0);

    for (int n = 0; n < N; ++n)
    {
        double sum = mag[0]; // DC
        for (int k = 1; k < halfN; ++k)
            sum += 2.0 * mag[(size_t) k] * std::cos (2.0 * juce::MathConstants<double>::pi * k * (n - center) / (double) N);
        sum += mag[(size_t) halfN] * std::cos (juce::MathConstants<double>::pi * (n - center));
        kernel[(size_t) n] = sum / (double) N;
    }

    // Apply Kaiser window
    double beta = 6.0;
    int M = N - 1;
    for (int i = 0; i < N; ++i)
    {
        double x = 2.0 * (double) i / (double) M - 1.0;
        double arg = beta * std::sqrt (std::max (0.0, 1.0 - x * x));
        double besselI0_arg = 1.0, besselI0_beta = 1.0;
        double tA = 1.0, tB = 1.0;
        for (int k = 1; k <= 20; ++k)
        {
            tA *= (arg / 2.0) / (double) k;
            tB *= (beta / 2.0) / (double) k;
            besselI0_arg += tA * tA;
            besselI0_beta += tB * tB;
        }
        kernel[(size_t) i] *= besselI0_arg / besselI0_beta;
    }

    // Check if it's a LP-type (DC gain > Nyquist gain) or HP-type
    double dcGain = 0;
    for (auto k : kernel) dcGain += k;

    if (std::abs (dcGain) > 1e-10)
    {
        for (auto& k : kernel) k /= dcGain; // normalize LP
    }
    else
    {
        // HP-type: normalize by alternating sum
        double nyGain = 0;
        for (int i = 0; i < N; ++i)
            nyGain += kernel[(size_t) i] * ((i % 2 == 0) ? 1.0 : -1.0);
        if (std::abs (nyGain) > 1e-10)
            for (auto& k : kernel) k /= nyGain;
    }

    applyKernel (kernel);
}

void LinearPhaseFIR::applyKernel (const std::vector<double>& kernel)
{
    if (kernel.size() != (size_t) FIR_SIZE) { active = false; return; }

    // JUCE FIR::Coefficients takes ORDER (= numTaps - 1), not numTaps
    auto coeffs = new juce::dsp::FIR::Coefficients<double> ((size_t)(FIR_SIZE - 1));
    auto* raw = coeffs->getRawCoefficients();
    for (int i = 0; i < FIR_SIZE; ++i)
        raw[i] = kernel[(size_t) i];

    auto ptr = juce::dsp::FIR::Coefficients<double>::Ptr (coeffs);
    firL.coefficients = ptr;
    firR.coefficients = ptr;

    // Reset delay lines to flush old kernel residuals
    firL.reset();
    firR.reset();

    active = true;
}

void LinearPhaseFIR::process (juce::dsp::AudioBlock<double>& block)
{
    if (!active || block.getNumChannels() < 2) return;

    auto chL = block.getSingleChannelBlock (0);
    auto chR = block.getSingleChannelBlock (1);
    juce::dsp::ProcessContextReplacing<double> ctxL (chL), ctxR (chR);
    firL.process (ctxL);
    firR.process (ctxR);
}

void LinearPhaseFIR::reset()
{
    firL.reset();
    firR.reset();
    active = false;
}

// ─────────────────────────────────────────────────────────────
//  INPUT STAGE
// ─────────────────────────────────────────────────────────────

void InputStage::prepare (double sr, int bs)
{
    currentSampleRate = sr; currentBlockSize = bs;
}

void InputStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (! stageOn.load()) return;
    updateInputMeters (block);
    const int n = (int)block.getNumSamples();

    double gain = inputGain.load (std::memory_order_relaxed);
    if (std::abs(gain - 1.0) > 1e-6) block.multiplyBy (gain);

    // M/S processing (global, no crossover split — widening is handled by Imager)
    double mg = midGain.load(), sg = sideGain.load();
    if (std::abs(mg - 1.0) > 1e-6 || std::abs(sg - 1.0) > 1e-6)
    {
        auto* l = block.getChannelPointer(0);
        auto* r = block.getChannelPointer(1);
        for (int i = 0; i < n; ++i)
        {
            double mid  = (l[i] + r[i]) * 0.5;
            double side = (l[i] - r[i]) * 0.5;
            mid *= mg; side *= sg;
            l[i] = mid + side;
            r[i] = mid - side;
        }
    }

    // Correlation
    auto* cL = block.getChannelPointer(0); auto* cR = block.getChannelPointer(1);
    double sLR=0, sLL=0, sRR=0;
    for (int i=0;i<n;++i) { sLR+=cL[i]*cR[i]; sLL+=cL[i]*cL[i]; sRR+=cR[i]*cR[i]; }
    double d=std::sqrt(sLL*sRR);
    correlation.store(d>1e-12 ? (float)(sLR/d) : 1.0f);

    updateOutputMeters (block);
}

void InputStage::reset() {}

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
}

void InputStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S1_Input_On")->load()>0.5f);
    inputGain.store(juce::Decibels::decibelsToGain((double)a.getRawParameterValue("S1_Input_Gain")->load()));
    midGain.store(juce::Decibels::decibelsToGain((double)a.getRawParameterValue("S1_Input_Mid_Gain")->load()));
    sideGain.store(juce::Decibels::decibelsToGain((double)a.getRawParameterValue("S1_Input_Side_Gain")->load()));
}

// ─────────────────────────────────────────────────────────────
//  PULTEC EQ STAGE
// ─────────────────────────────────────────────────────────────

void PultecEQStage::prepare (double sr, int bs)
{
    currentSampleRate = sr; currentBlockSize = bs;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 1 };
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
    updateFilters();
}

void PultecEQStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters (block);
    int n = (int) block.getNumSamples();
    auto* l = block.getChannelPointer (0);
    auto* r = block.getChannelPointer (1);

    for (int i = 0; i < n; ++i)
    {
        double L = l[i], R = r[i];

        // ─── EQP-1A Low section (boost shelf + inductor overshoot + atten) ───
        L = lowShelfL.processSample (L);         R = lowShelfR.processSample (R);
        L = lowResonanceL.processSample (L);     R = lowResonanceR.processSample (R);
        L = lowAttenL.processSample (L);         R = lowAttenR.processSample (R);

        // ─── EQP-1A High section (peak + air + atten shelf) ───
        L = highPeakL.processSample (L);         R = highPeakR.processSample (R);
        L = highAirL.processSample (L);          R = highAirR.processSample (R);
        L = highAttenL.processSample (L);        R = highAttenR.processSample (R);

        // ─── MEQ-5 (with inductor overshoot) ───
        L = lowMidL.processSample (L);           R = lowMidR.processSample (R);
        L = lowMidSkirtL.processSample (L);      R = lowMidSkirtR.processSample (R);
        L = midDipL.processSample (L);           R = midDipR.processSample (R);
        L = midDipSkirtL.processSample (L);      R = midDipSkirtR.processSample (R);
        L = highMidL.processSample (L);          R = highMidR.processSample (R);
        L = highMidSkirtL.processSample (L);     R = highMidSkirtR.processSample (R);

        // ─── 12AX7 tube makeup stage — subtle even harmonics ───
        L = tubeSaturate (L);                    R = tubeSaturate (R);

        // ─── Output transformer — gentle HF rolloff ───
        L = xfmrL.processSample (L);            R = xfmrR.processSample (R);

        l[i] = L; r[i] = R;
        pushSampleToFFT ((float)((L + R) * 0.5));
    }
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

void PultecEQStage::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S2_EQ_On","Pultec EQ On",true));
    // EQP-1A LOW
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_LowBoost_Freq","Low Freq",juce::StringArray{"20","30","60","100"},2));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowBoost_Gain","Low Boost",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowAtten_Gain","Low Atten",juce::NormalisableRange<float>(0,10,0.1f),0));
    // EQP-1A HIGH
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_HighBoost_Freq","High Freq",juce::StringArray{"3k","4k","5k","8k","10k","12k","16k"},0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighBoost_Gain","High Boost",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighAtten_Gain","High Atten",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("S2_EQ_HighAtten_Freq","Atten Sel",juce::StringArray{"5k","10k","20k"},1));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighAtten_BW","Bandwidth",juce::NormalisableRange<float>(0,10,0.1f),5));
    // MEQ-5
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowMid_Freq","LM Freq",juce::NormalisableRange<float>(200,1000,1,0.4f),200));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_LowMid_Gain","LM Peak",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_MidDip_Freq","Dip Freq",juce::NormalisableRange<float>(200,7000,1,0.35f),1000));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_MidDip_Gain","Dip",juce::NormalisableRange<float>(0,10,0.1f),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighMid_Freq","HM Freq",juce::NormalisableRange<float>(1500,5000,1,0.3f),1500));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S2_EQ_HighMid_Gain","HM Peak",juce::NormalisableRange<float>(0,10,0.1f),0));
}

void PultecEQStage::updateParameters (const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store (a.getRawParameterValue("S2_EQ_On")->load() > 0.5f);
    static const float lowFreqs[] = { 20.0f, 30.0f, 60.0f, 100.0f };
    int lowIdx = juce::jlimit (0, 3, (int) a.getRawParameterValue("S2_EQ_LowBoost_Freq")->load());
    lowBoostFreq.store (lowFreqs[lowIdx]);
    lowBoostGain.store (a.getRawParameterValue("S2_EQ_LowBoost_Gain")->load());
    lowAttenGain.store (a.getRawParameterValue("S2_EQ_LowAtten_Gain")->load());
    static const float highFreqs[] = { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
    int highIdx = juce::jlimit (0, 6, (int) a.getRawParameterValue("S2_EQ_HighBoost_Freq")->load());
    highBoostFreq.store (highFreqs[highIdx]);
    highBoostGain.store (a.getRawParameterValue("S2_EQ_HighBoost_Gain")->load());
    highAttenGain.store (a.getRawParameterValue("S2_EQ_HighAtten_Gain")->load());
    static const float hAttenFreqs[] = { 5000.0f, 10000.0f, 20000.0f };
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
    xover1LP.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    xover1HP.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    xover2LP.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    xover2HP.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    xover3LP.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    xover3HP.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    for (auto& b:bandBuffers) b.setSize(2,bs);
    tempBuffer.setSize(2,bs);
}

void SaturationStage::process (juce::dsp::AudioBlock<double>& block)
{
    if (!stageOn.load()) return;
    updateInputMeters(block);
    int n=(int)block.getNumSamples();

    if (mode.load()==0)
    {
        // Single-band mode
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

                // Sample rate reduction for Bitcrush
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
    else
    {
        // ─── MULTIBAND MODE — proper 4-way crossover split ──
        for (auto& buf:bandBuffers) buf.setSize(2,n,false,false,true);
        tempBuffer.setSize(2,n,false,false,true);

        // Update crossover frequencies
        xover1LP.setCutoffFrequency(xoverFreq1.load());
        xover1HP.setCutoffFrequency(xoverFreq1.load());
        xover2LP.setCutoffFrequency(xoverFreq2.load());
        xover2HP.setCutoffFrequency(xoverFreq2.load());
        xover3LP.setCutoffFrequency(xoverFreq3.load());
        xover3HP.setCutoffFrequency(xoverFreq3.load());

        // Step 1: Split at xover1 → low (band1) and high remainder
        for (int ch=0;ch<2;++ch)
        {
            auto* src=block.getChannelPointer(ch);
            juce::FloatVectorOperations::copy(bandBuffers[0].getWritePointer(ch),src,n);
            juce::FloatVectorOperations::copy(tempBuffer.getWritePointer(ch),src,n);
        }
        { juce::dsp::AudioBlock<double> b(bandBuffers[0]); juce::dsp::ProcessContextReplacing<double> c(b); xover1LP.process(c); }
        { juce::dsp::AudioBlock<double> b(tempBuffer); juce::dsp::ProcessContextReplacing<double> c(b); xover1HP.process(c); }

        // Step 2: Split remainder at xover2 → mid-low (band2) and high remainder
        for (int ch=0;ch<2;++ch)
        {
            juce::FloatVectorOperations::copy(bandBuffers[1].getWritePointer(ch),tempBuffer.getReadPointer(ch),n);
            juce::FloatVectorOperations::copy(bandBuffers[2].getWritePointer(ch),tempBuffer.getReadPointer(ch),n);
        }
        { juce::dsp::AudioBlock<double> b(bandBuffers[1]); juce::dsp::ProcessContextReplacing<double> c(b); xover2LP.process(c); }
        { juce::dsp::AudioBlock<double> b(bandBuffers[2]); juce::dsp::ProcessContextReplacing<double> c(b); xover2HP.process(c); }

        // Step 3: Split remainder at xover3 → mid-high (band3) and high (band4)
        for (int ch=0;ch<2;++ch)
            juce::FloatVectorOperations::copy(bandBuffers[3].getWritePointer(ch),bandBuffers[2].getReadPointer(ch),n);
        { juce::dsp::AudioBlock<double> b(bandBuffers[2]); juce::dsp::ProcessContextReplacing<double> c(b); xover3LP.process(c); }
        { juce::dsp::AudioBlock<double> b(bandBuffers[3]); juce::dsp::ProcessContextReplacing<double> c(b); xover3HP.process(c); }

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

    updateOutputMeters(block);
}

void SaturationStage::reset()
{ xover1LP.reset();xover1HP.reset();xover2LP.reset();xover2HP.reset();xover3LP.reset();xover3HP.reset();
  fifoIndex=0; fftReady.store(false); globalSRCounter=0; globalSRHold=0;
  for(int i=0;i<4;++i){srCounter[i]=0;srHoldSample[i]=0;}
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
    double x = input * driveLinear;
    switch (type)
    {
        case 0: return std::tanh (x);                                           // Tape
        case 1: return x >= 0 ? 1 - std::exp (-x) : -(1 - std::exp (x)) * 0.8; // Tube
        case 2: return x / (1 + std::abs (x));                                  // Transistor
        case 3: return juce::jlimit (-1.0, 1.0, x);                            // Digital
        case 4: {                                                                // Bitcrush
            double lv = std::pow (2.0, bitsVal);
            return std::round (x * lv) / lv;
        }
        default: return std::tanh (x);
    }
}

void SaturationStage::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add(std::make_unique<juce::AudioParameterBool>("S4_Sat_On","Sat On",true));
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
    for (int b = 0; b < NUM_BANDS; ++b) { bandL[b].prepare(spec); bandR[b].prepare(spec); }
    // Default init
    freq[0].store(100); freq[1].store(400); freq[2].store(1000); freq[3].store(3500); freq[4].store(8000);
    for (int b = 0; b < NUM_BANDS; ++b) { gain[b].store(0); q[b].store(0.707f); }
    updateFilters();
}

void OutputEQStage::process(juce::dsp::AudioBlock<double>& block)
{
    if(!stageOn.load())return; updateInputMeters(block);
    int n=(int)block.getNumSamples(); auto*l=block.getChannelPointer(0); auto*r=block.getChannelPointer(1);
    for(int i=0;i<n;++i)
    {
        double L=l[i], R=r[i];
        for (int b = 0; b < NUM_BANDS; ++b)
        { L = bandL[b].processSample(L); R = bandR[b].processSample(R); }
        l[i]=L; r[i]=R;
        pushSampleToFFT ((float)((L + R) * 0.5));
    }
    updateOutputMeters(block);
}

void OutputEQStage::reset()
{
    for (int b = 0; b < NUM_BANDS; ++b) { bandL[b].reset(); bandR[b].reset(); }
    fftFifoIndex = 0; fftReady.store(false);
}

void OutputEQStage::updateFilters()
{
    double sr=currentSampleRate; if(sr<=0)return;
    // Band 0: Low Shelf
    auto ls=juce::dsp::IIR::Coefficients<double>::makeLowShelf(sr,freq[0].load(),(double)q[0].load(),juce::Decibels::decibelsToGain((double)gain[0].load()));
    *bandL[0].coefficients=*ls; *bandR[0].coefficients=*ls;
    // Bands 1-3: Peak
    for (int b = 1; b <= 3; ++b)
    {
        auto pk=juce::dsp::IIR::Coefficients<double>::makePeakFilter(sr,freq[b].load(),(double)q[b].load(),juce::Decibels::decibelsToGain((double)gain[b].load()));
        *bandL[b].coefficients=*pk; *bandR[b].coefficients=*pk;
    }
    // Band 4: High Shelf
    auto hs=juce::dsp::IIR::Coefficients<double>::makeHighShelf(sr,freq[4].load(),(double)q[4].load(),juce::Decibels::decibelsToGain((double)gain[4].load()));
    *bandL[4].coefficients=*hs; *bandR[4].coefficients=*hs;
}

double OutputEQStage::getMagnitudeAtFreq (double f) const
{
    double sr = currentSampleRate; if (sr <= 0) return 0.0;
    double mag = 1.0;
    for (int b = 0; b < NUM_BANDS; ++b)
        if (bandL[b].coefficients) mag *= bandL[b].coefficients->getMagnitudeForFrequency (f, sr);
    return juce::Decibels::gainToDecibels (mag, -60.0);
}

OutputEQStage::BandInfo OutputEQStage::getBandInfo (int band) const
{
    int type = (band == 0) ? 0 : (band == 4) ? 2 : 1;
    return { freq[band].load(), gain[band].load(), q[band].load(), type };
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
}

void OutputEQStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S5_EQ2_On")->load()>0.5f);
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
    linPhaseHP.prepare (sr, bs);
    linPhaseLP.prepare (sr, bs);
    linPhaseBuilt = false;
    lastHPFreq = -1; lastLPFreq = -1; lastHPSlope = -1; lastLPSlope = -1;
    lastHPOn = false; lastLPOn = false;
    updateFilters();
}

void FilterStage::process(juce::dsp::AudioBlock<double>& block)
{
    if(!stageOn.load())return; updateInputMeters(block);

    bool isLinear = (filterMode.load() == 1);

    if (isLinear && linPhaseBuilt)
    {
        // ─── Linear Phase mode ───
        if (hpOn.load() && linPhaseHP.isActive())
            linPhaseHP.process (block);
        if (lpOn.load() && linPhaseLP.isActive())
            linPhaseLP.process (block);
    }
    else if (!isLinear)
    {
        // ─── Minimum Phase mode (IIR cascade) ───
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
    }
    updateOutputMeters(block);
}

void FilterStage::reset()
{
    for(int i=0;i<MAX_STAGES;++i){hpL[i].reset();hpR[i].reset();lpL[i].reset();lpR[i].reset();}
    linPhaseHP.reset(); linPhaseLP.reset();
    linPhaseBuilt = false;
}

int FilterStage::getLatencySamples()const
{
    if (filterMode.load() == 1 && linPhaseBuilt)
    {
        // Each active FIR adds latency (they're in series)
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

    // ─── Build HP FIR ───
    if (hpOn.load())
    {
        float freq = hpFreq.load();
        int slopeIdx = hpSlope.load();
        int stages = (slopeIdx == 4) ? 4 : slopeIdx + 1; // 0→1, 1→2, 2→3, 3→4, 4→4

        if (stages == 1)
        {
            // Single 6dB/oct: simple windowed-sinc highpass is too steep.
            // Use IIR magnitude sampling for gentle slope.
            std::vector<juce::dsp::IIR::Coefficients<double>::Ptr> c;
            c.push_back (juce::dsp::IIR::Coefficients<double>::makeFirstOrderHighPass (sr, (double)freq));
            linPhaseHP.designFromIIRMagnitude (c, sr);
        }
        else
        {
            // Multi-stage: cascade Butterworth sections for correct slope
            std::vector<juce::dsp::IIR::Coefficients<double>::Ptr> c;
            for (int s = 0; s < stages && s < MAX_STAGES; ++s)
                c.push_back (juce::dsp::IIR::Coefficients<double>::makeHighPass (sr, (double)freq, 0.707));
            linPhaseHP.designFromIIRMagnitude (c, sr);
        }
    }
    else
    {
        linPhaseHP.reset();
    }

    // ─── Build LP FIR ───
    if (lpOn.load())
    {
        float freq = lpFreq.load();
        int slopeIdx = lpSlope.load();
        int stages = (slopeIdx == 4) ? 4 : slopeIdx + 1;

        if (stages == 1)
        {
            std::vector<juce::dsp::IIR::Coefficients<double>::Ptr> c;
            c.push_back (juce::dsp::IIR::Coefficients<double>::makeFirstOrderLowPass (sr, (double)freq));
            linPhaseLP.designFromIIRMagnitude (c, sr);
        }
        else
        {
            std::vector<juce::dsp::IIR::Coefficients<double>::Ptr> c;
            for (int s = 0; s < stages && s < MAX_STAGES; ++s)
                c.push_back (juce::dsp::IIR::Coefficients<double>::makeLowPass (sr, (double)freq, 0.707));
            linPhaseLP.designFromIIRMagnitude (c, sr);
        }
    }
    else
    {
        linPhaseLP.reset();
    }

    linPhaseBuilt = true;
}

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

    // ─── Rebuild linear phase FIR when ANY relevant param changes ───
    if (newMode == 1)
    {
        bool needRebuild = !linPhaseBuilt
            || curHPOn != lastHPOn || curLPOn != lastLPOn
            || std::abs (curHP - lastHPFreq) > 0.5f
            || std::abs (curLP - lastLPFreq) > 0.5f
            || curHPS != lastHPSlope || curLPS != lastLPSlope;

        if (needRebuild)
        {
            rebuildLinearPhase();
            lastHPFreq = curHP; lastLPFreq = curLP;
            lastHPSlope = curHPS; lastLPSlope = curLPS;
            lastHPOn = curHPOn; lastLPOn = curLPOn;
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
    float hiFreq = highFreq.load();

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
                targetGainDb = -excessDb * depthScale;
                targetGainDb = juce::jmax(targetGainDb, -18.0f);  // Max 18dB cut
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
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_Depth","Depth",juce::NormalisableRange<float>(0,100,1),0));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_Sensitivity","Selectivity",juce::NormalisableRange<float>(0,100,1),50));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_Sharpness","Sharpness",juce::NormalisableRange<float>(0,100,1),50));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_Speed","Speed",juce::NormalisableRange<float>(0,100,1),50));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_LowFreq","Low Freq",juce::NormalisableRange<float>(20,2000,1,0.3f),200));
    layout.add(std::make_unique<juce::AudioParameterFloat>("S6B_DynEQ_HighFreq","High Freq",juce::NormalisableRange<float>(1000,20000,1,0.3f),12000));
}

void DynamicResonanceStage::updateParameters(const juce::AudioProcessorValueTreeState& a)
{
    stageOn.store(a.getRawParameterValue("S6B_DynEQ_On")->load()>0.5f);
    depth.store(a.getRawParameterValue("S6B_DynEQ_Depth")->load());
    selectivity.store(a.getRawParameterValue("S6B_DynEQ_Sensitivity")->load());
    sharpness.store(a.getRawParameterValue("S6B_DynEQ_Sharpness")->load());
    speed.store(a.getRawParameterValue("S6B_DynEQ_Speed")->load());
    lowFreq.store(a.getRawParameterValue("S6B_DynEQ_LowFreq")->load());
    highFreq.store(a.getRawParameterValue("S6B_DynEQ_HighFreq")->load());
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
            // Attack — style determines speed
            double attackMs;
            switch(limSt)
            {
                case 1:  attackMs = 0.02; break;   // Aggressive: 0.02ms ultra-fast
                case 2:  attackMs = 0.2;  break;   // Warm: 0.2ms slightly slower
                default: attackMs = 0.1;  break;   // Transparent: 0.1ms
            }
            double ac = std::exp(-1.0/(currentSampleRate * attackMs / 1000.0));
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
            // Style affects release character
            switch(limSt)
            {
                case 1:  rc = std::pow(rc, 1.5); break;  // Aggressive: faster release (pumpy)
                case 2:  rc = std::pow(rc, 0.5); break;  // Warm: much slower release (smooth)
                default: break;                           // Transparent: as set
            }
            grEnvelope=rc*grEnvelope+(1-rc)*tg;
        }

        int rIdx=(delayWritePos+i-lookaheadSamples+delSz)%delSz;
        for(int ch=0;ch<nch&&ch<2;++ch)
        {
            double sample = delayBuffer.getSample(ch,rIdx) * grEnvelope;
            // HARD CLIP SAFETY — never exceed ceiling
            sample = juce::jlimit(-ceil, ceil, sample);
            block.setSample(ch,i,sample);
        }

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

        // ─── Apply per-band M/S width processing ───
        bool anyWidthChanged = false;
        for (int bnd = 0; bnd < NUM_IMG_BANDS; ++bnd)
            if (std::abs (bandWidthValues[bnd].load() - 100.0f) > 0.5f) { anyWidthChanged = true; break; }

        if (anyWidthChanged)
        {
            for (int bnd = 0; bnd < NUM_IMG_BANDS; ++bnd)
            {
                float w = bandWidthValues[bnd].load() / 100.0f;
                auto* bL = imgBandBufs[bnd].getWritePointer (0);
                auto* bR = imgBandBufs[bnd].getWritePointer (1);
                for (int i = 0; i < n; ++i)
                {
                    float mid  = (bL[i] + bR[i]) * 0.5f;
                    float side = (bL[i] - bR[i]) * 0.5f;
                    side *= w;
                    bL[i] = mid + side;
                    bR[i] = mid - side;
                }
            }
            // Recombine bands into buffer
            auto* outL = buf.getWritePointer (0);
            auto* outR = buf.getWritePointer (1);
            for (int i = 0; i < n; ++i) { outL[i] = 0; outR[i] = 0; }
            for (int bnd = 0; bnd < NUM_IMG_BANDS; ++bnd)
            {
                auto* bL = imgBandBufs[bnd].getReadPointer (0);
                auto* bR = imgBandBufs[bnd].getReadPointer (1);
                for (int i = 0; i < n; ++i) { outL[i] += bL[i]; outR[i] += bR[i]; }
            }
        }
    }

    // ─── FFT FIFO ───
    for (int i = 0; i < n; ++i)
    {
        float sample = (rL[i] + rR[i]) * 0.5f;
        fifo[(size_t) fifoIndex] = sample;
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
    fftWindow.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fftProcessor.performFrequencyOnlyForwardTransform (fftData.data());
    auto minDb = -80.0f, maxDb = 0.0f;
    for (int i = 0; i < fftSize / 2; ++i)
    {
        auto level = juce::Decibels::gainToDecibels (fftData[(size_t)i] / (float) fftSize, minDb);
        magnitudes[(size_t)i] = juce::jmap (level, minDb, maxDb, 0.0f, 1.0f);
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
    reorderableStages[5]=std::make_unique<DynamicResonanceStage>();
    reorderableStages[6]=std::make_unique<ClipperStage>();
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

    limiterStage->process(osBlock);

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
        // Fast convergence for initial measurement
        if (smoothedInputLoudness < -80.0f)
            smoothedInputLoudness = inputLoudness;
        else
            smoothedInputLoudness = smoothedInputLoudness * 0.7f + inputLoudness * 0.3f;
    }
    else
    {
        // Reset when off so it converges fast when turned back on
        smoothedInputLoudness = -100.0f;
        smoothedOutputLoudness = -100.0f;
        smoothedMatchGain = 1.0f;
    }

    engine.updateAllParameters(apvts);
    setLatencySamples(engine.getTotalLatency());
    engine.process(buf);

    // Auto-match: measure output loudness and compensate to match input LUFS
    if (autoMatch && smoothedInputLoudness > -80.0f)
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
        float outputLoudness = (float)(-0.691 + 10.0 * std::log10 (std::max (sumPower / (double)(n * nch), 1e-10)));

        if (smoothedOutputLoudness < -80.0f)
            smoothedOutputLoudness = outputLoudness;
        else
            smoothedOutputLoudness = smoothedOutputLoudness * 0.7f + outputLoudness * 0.3f;

        // Compensate: difference in LUFS = gain to apply
        float diffDb = smoothedInputLoudness - smoothedOutputLoudness;
        diffDb = juce::jlimit (-12.0f, 12.0f, diffDb);
        float targetGain = juce::Decibels::decibelsToGain (diffDb);
        smoothedMatchGain = smoothedMatchGain * 0.85f + targetGain * 0.15f;
        buf.applyGain (smoothedMatchGain);
    }

    // Metering AFTER auto-match so LUFS shows what user actually hears
    if (auto* om = engine.getOutputMeter())
    {
        om->process (buf);
        om->applySolo (buf);  // band solo monitoring
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
