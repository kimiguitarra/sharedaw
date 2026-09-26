// 動作確認用のテストプラグイン。
#include <juce_audio_utils/juce_audio_utils.h>

#if COLLAB_TEST_CRASH
// CollabDAW（スキャン用の子プロセスを含む）にモジュールが読み込まれた時点でクラッシュする
static struct CrashOnLoad
{
    CrashOnLoad()
    {
        if (juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFileName().contains ("CollabDAW"))
        {
            volatile int* p = nullptr;
            *p = 1;
        }
    }
} crashOnLoad;
#endif

class TestSynth  : public juce::AudioProcessor
{
public:
    TestSynth()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (gain = new juce::AudioParameterFloat ({ "gain", 1 }, "Gain", 0.0f, 1.0f, 0.5f));
    }

    const juce::String getName() const override             { return JucePlugin_Name; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.0; }
    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    bool hasEditor() const override                         { return true; }
    juce::AudioProcessorEditor* createEditor() override     { return new juce::GenericAudioProcessorEditor (*this); }

    void prepareToPlay (double sr, int) override            { sampleRate = sr; }
    void releaseResources() override                        {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        int pos = 0;

        auto render = [&] (int until)
        {
            for (; pos < until; ++pos)
            {
                const float v = note >= 0 ? std::sin (phase) * gain->get() * 0.5f : 0.0f;
                phase += juce::MathConstants<float>::twoPi * (float) (juce::MidiMessage::getMidiNoteInHertz (juce::jmax (0, note)) / sampleRate);

                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.setSample (ch, pos, v);
            }
        };

        for (const auto m : midi)
        {
            render (m.samplePosition);
            auto msg = m.getMessage();

            if (msg.isNoteOn())                               note = msg.getNoteNumber();
            else if (msg.isNoteOff() && msg.getNoteNumber() == note) note = -1;
        }

        render (buffer.getNumSamples());
    }

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::MemoryOutputStream (dest, false).writeFloat (gain->get());
    }

    void setStateInformation (const void* data, int size) override
    {
        if (size >= 4)
            *gain = juce::MemoryInputStream (data, (size_t) size, false).readFloat();
    }

private:
    juce::AudioParameterFloat* gain = nullptr;
    double sampleRate = 48000.0;
    float phase = 0.0f;
    int note = -1;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()    { return new TestSynth(); }
