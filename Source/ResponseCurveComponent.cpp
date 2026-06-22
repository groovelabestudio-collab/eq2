#include "ResponseCurveComponent.h"

ResponseCurveComponent::ResponseCurveComponent()
{
    startTimerHz(refreshRateHz);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
    stopTimer();
}

void ResponseCurveComponent::prepareAnalyzer(int numBins, double sr) noexcept
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    currentNumBins = numBins;
    bufferWrite.assign(numBins, 0.0f);
    smoothedMagnitudes.assign(numBins, minDb);
}

void ResponseCurveComponent::pushFFTDataFromAudioThread(const float* mags, int numBins) noexcept
{
    if (numBins <= 0) return;

    // if sizes mismatch, ignore (caller should call prepareAnalyzer first)
    if (numBins != currentNumBins)
        return;

    // copy raw magnitudes into preallocated bufferWrite (no allocs)
    std::memcpy(bufferWrite.data(), mags, sizeof(float) * (size_t)numBins);

    // publish (release semantics)
    hasNewData.store(true, std::memory_order_release);
}

float ResponseCurveComponent::magnitudeToDbFloor(float mag) const noexcept
{
    constexpr float epsilon = 1e-12f;
    float safe = std::max(mag, epsilon);
    return juce::Decibels::gainToDecibels(safe);
}

float ResponseCurveComponent::coeffFromTimeMs(float timeMs, float refreshHz) const noexcept
{
    if (timeMs <= 0.0f || refreshHz <= 0.0f) return 0.0f;
    float tau = timeMs * 0.001f;
    return std::exp(-1.0f / (tau * refreshHz));
}

float ResponseCurveComponent::freqForBin(int binIndex) const noexcept
{
    // bins 0..N-1 map to 20 Hz .. Nyquist (log scale)
    if (currentNumBins <= 1) return 20.0f;
    float minF = 20.0f;
    float maxF = float(sampleRate * 0.5);
    float norm = (float)binIndex / (float)(currentNumBins - 1);
    float logMin = std::log10(minF);
    float logMax = std::log10(maxF);
    float logF = logMin + norm * (logMax - logMin);
    return std::pow(10.0f, logF);
}

void ResponseCurveComponent::timerCallback()
{
    // Acquire flag (acquire semantics ensures we see prior writes)
    if (!hasNewData.load(std::memory_order_acquire))
        return;

    // Copy bufferWrite into a local vector for processing to minimize UI stalls.
    // bufferWrite is preallocated; small race is acceptable for visualization.
    std::vector<float> input;
    input = bufferWrite; // copy (fast: N floats)

    // reset flag
    hasNewData.store(false, std::memory_order_release);

    // Ensure smoothedMagnitudes sized
    if ((int)smoothedMagnitudes.size() != (int)input.size())
        smoothedMagnitudes.assign((int)input.size(), minDb);

    float attackCoeff = coeffFromTimeMs(attackTimeMs, (float)refreshRateHz);
    float decayCoeff  = coeffFromTimeMs(decayTimeMs,  (float)refreshRateHz);

    for (int i = 0; i < (int)input.size(); ++i)
    {
        float db = magnitudeToDbFloor(input[(size_t)i]);
        db = juce::jlimit(minDb, maxDb, db);

        float prev = smoothedMagnitudes[(size_t)i];
        float coeff = (db > prev) ? attackCoeff : decayCoeff;
        float sm = coeff * prev + (1.0f - coeff) * db;
        smoothedMagnitudes[(size_t)i] = sm;
    }

    repaint();
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // background
    g.fillAll(juce::Colours::black);

    if (smoothedMagnitudes.empty())
        return;

    float topPad = bounds.getHeight() * 0.03f;
    float areaH = bounds.getHeight() - topPad * 2.0f;
    juce::Rectangle<float> area(bounds.getX(), bounds.getY() + topPad, bounds.getWidth(), areaH);

    juce::Path p;
    int N = (int)smoothedMagnitudes.size();
    for (int i = 0; i < N; ++i)
    {
        float db = smoothedMagnitudes[(size_t)i];
        float norm = (db - minDb) / (maxDb - minDb);
        norm = juce::jlimit(0.0f, 1.0f, norm);
        norm *= displayScale;

        float freq = freqForBin(i);
        float xNorm = (std::log10(freq) - std::log10(20.0f)) / (std::log10(sampleRate * 0.5f) - std::log10(20.0f));
        xNorm = juce::jlimit(0.0f, 1.0f, xNorm);
        float x = area.getX() + xNorm * area.getWidth();
        float y = area.getBottom() - norm * area.getHeight();

        if (i == 0) p.startNewSubPath(x, y);
        else p.lineTo(x, y);
    }

    // draw curve
    g.setColour(juce::Colours::orange);
    g.strokePath(p, juce::PathStrokeType(1.5f));

    // draw reference db lines
    g.setFont(12.0f);
    for (float dbLine = -60.0f; dbLine <= 12.0f; dbLine += 12.0f)
    {
        float norm = (dbLine - minDb) / (maxDb - minDb);
        norm = juce::jlimit(0.0f, 1.0f, norm) * displayScale;
        float y = area.getBottom() - norm * area.getHeight();
        g.setColour(juce::Colours::grey.withAlpha(0.25f));
        g.drawHorizontalLine((int)std::round(y), area.getX(), area.getRight());
        g.setColour(juce::Colours::grey);
        g.drawText(juce::String((int)dbLine) + " dB", 2, (int)std::round(y) - 8, 40, 16, juce::Justification::left);
    }
}
