#pragma once
#include <JuceHeader.h>

// ResponseCurveComponent
// - Recibe magnitudes FFT desde el audio thread usando pushFFTDataFromAudioThread()
// - Suaviza magnitudes en dB y dibuja la curva en escala log-freq.
// - Diseñado para visualización solamente; no hace allocs en audio thread.

class ResponseCurveComponent : public juce::Component,
                               private juce::Timer
{
public:
    ResponseCurveComponent();
    ~ResponseCurveComponent() override;

    // Llamar desde el audio thread (evita allocations; copia simple de floats).
    // mags: puntero a floats con numBins elementos (lineal magnitudes)
    // NOTA: el audio thread NO debe llamar con un numBins distinto al esperado sin reconfigurar.
    void pushFFTDataFromAudioThread(const float* mags, int numBins) noexcept;

    // Llamar desde el hilo principal cuando cambie sampleRate/FFT size
    void prepareAnalyzer(int numBins, double sampleRate) noexcept;

    // Opcionales: setters para parámetros visuales
    void setDisplayScale(float s) noexcept { displayScale = juce::jlimit(0.5f, 1.0f, s); }
    void setMinDb(float v) noexcept { minDb = v; }
    void setMaxDb(float v) noexcept { maxDb = v; }
    void setAttackMs(float v) noexcept { attackTimeMs = v; }
    void setDecayMs(float v) noexcept { decayTimeMs = v; }
    void setRefreshHz(int hz) noexcept { refreshRateHz = hz; startTimerHz(refreshRateHz); }

    void paint(juce::Graphics& g) override;
    void resized() override {}

private:
    // GUI timer
    void timerCallback() override;

    // Conversion / smoothing helpers
    float magnitudeToDbFloor(float mag) const noexcept;
    float coeffFromTimeMs(float timeMs, float refreshHz) const noexcept;
    float freqForBin(int binIndex) const noexcept;

    // Buffers (double buffer style, preallocated)
    std::vector<float> bufferWrite; // escrito por audio thread (preallocado)
    std::vector<float> smoothedMagnitudes; // guardado en dB, usado por paint()
    std::atomic<bool> hasNewData { false };
    int currentNumBins = 0;

    // Analyzer settings
    double sampleRate = 44100.0;
    float minDb = -100.0f;
    float maxDb = 12.0f;
    float attackTimeMs = 6.0f;
    float decayTimeMs = 250.0f;
    int refreshRateHz = 60;
    float displayScale = 0.92f; // headroom

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseCurveComponent)
};
