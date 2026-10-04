// EngineBridge の書き出し: ミックスダウン・パラデータ・バウンス、余韻の長さ、音の元のフィンガープリント

#include "EngineBridge.h"
#include "EngineBridgeDetail.h"

#include "collab/ClipEditing.h"
#include <sstream>
#include "SfizzPlugin.h"
#include "audio/ChannelStripPlugin.h"
#include "audio/BuiltinEffectPlugin.h"
#include "audio/MasterLimiterPlugin.h"
#include "audio/CountInPlugin.h"
#include "collab/Recording.h"
#include "collab/ChordPlayback.h"
#include "collab/Render.h"
#include "audio/AudioFiles.h"
#include "collab/Time.h"
#include "collab/Uuid.h"
#include "plugins/PluginHost.h"

using namespace EngineBridgeDetail;

double EngineBridge::tailSecondsFor (const std::string& trackId, double minimum) const
{
    double tail = minimum;
    const auto& project = document.getProject();

    for (auto& t : project.tracks)
    {
        // 対象のトラックと、その出力先・センド先のバス（バスのリバーブの余韻も入る）
        bool relevant = trackId.empty() || t.id == trackId;

        if (! relevant)
            if (auto* src = project.findTrack (trackId))
                relevant = src->output == t.id || std::any_of (src->sends.begin(), src->sends.end(), [&] (auto& s) { return s.busId == t.id; });

        if (! relevant)
            continue;

        for (auto& e : t.effects)
        {
            if (e.bypass)
                continue;

            if (auto type = collab::fx::typeFromId (e.builtin))
                tail = std::max (tail, collab::fx::tailSeconds (*type, e.params) + 0.5);
            else if (auto* p = getExternalPlugin (t.id, e.id))
                tail = std::max (tail, std::min (30.0, p->getTailLength() + 0.5));
        }
    }

    return std::min (tail, 30.0);
}

juce::Result EngineBridge::renderTrack (const std::string& trackId, const juce::File& output, double tailSeconds)
{
    const auto& project = document.getProject();
    const double end = document.getTempoMap().tickToSeconds ((double) collab::chordTrackEndTick (project, document.getTempoMap())) + tailSeconds;
    return renderOneTrack (trackId, output, end, false, 32);
}

juce::Result EngineBridge::renderStem (const std::string& trackId, const juce::File& output, double endSeconds, int bitDepth)
{
    if (! trackId.empty())
        return renderOneTrack (trackId, output, endSeconds, true, bitDepth);

    // コードトラック
    sync();
    stop();

    if (chordTrack == nullptr)
        return juce::Result::fail ("コードトラックがありません"_ju);

    // ふだんはミックス用のトラックを通って鳴る。単独で書き出すときは直接出す（通さないと何も聞こえない）
    std::vector<te::Plugin::Ptr> noSends;
    const ScopedIsolatedTrack isolated (*chordTrack, noSends, nullptr, false);
    const auto* chord = chordTrack.get();
    const bool ok = renderTracksToWav (tracksMatching ([chord] (te::Track* t) { return t == chord; }),
                                       output, endSeconds, bitDepth, (double) collab::kSampleRate);

    return ok ? juce::Result::ok() : juce::Result::fail ("書き出しに失敗しました"_ju);
}

juce::BigInteger EngineBridge::tracksMatching (const std::function<bool (te::Track*)>& pred) const
{
    juce::BigInteger mask;
    auto all = te::getAllTracks (*edit);

    for (int i = 0; i < all.size(); ++i)
        if (pred (all[i]))
            mask.setBit (i);

    return mask;
}

EngineBridge::ScopedIsolatedTrack::ScopedIsolatedTrack (te::AudioTrack& t, std::vector<te::Plugin::Ptr>& s, te::Plugin* stripToBypass, bool dry)
    : track (t), sends (s), strip (stripToBypass)
{
    // バウンス（dry）はトラックの音量・パン・ミュートの前の音（受け取った側でも同じ設定がかかるため）
    if (dry)
        volume = track.getVolumePlugin();

    if (volume != nullptr)
    {
        oldDb = volume->getVolumeDb();
        oldPan = volume->getPan();
        volume->setVolumeDb (0.0f);
        volume->setPan (0.0f);
    }

    oldMute = track.isMuted (false);
    track.setMute (false);

    // バスへの出力・センドは通さない（トラックそのものの音を書き出す）
    oldDest = track.getOutput().getDestinationTrack();

    if (oldDest != nullptr)
        track.getOutput().setOutputToDefaultDevice (false);

    for (auto& send : sends)
    {
        sendsWereEnabled.push_back (send->isEnabled());
        send->setEnabled (false);
    }

    if (strip != nullptr)
    {
        stripWasEnabled = strip->isEnabled();
        strip->setEnabled (false);
    }
}

EngineBridge::ScopedIsolatedTrack::~ScopedIsolatedTrack()
{
    if (volume != nullptr)
    {
        volume->setVolumeDb (oldDb);
        volume->setPan (oldPan);
    }

    track.setMute (oldMute);

    if (strip != nullptr)
        strip->setEnabled (stripWasEnabled);

    if (oldDest != nullptr)
        track.getOutput().setOutputToTrack (oldDest);

    for (size_t i = 0; i < sends.size() && i < sendsWereEnabled.size(); ++i)
        sends[i]->setEnabled (sendsWereEnabled[i]);
}

bool EngineBridge::renderTracksToWav (const juce::BigInteger& tracksToDo, const juce::File& output, double endSeconds, int bitDepth, double sampleRate)
{
    juce::WavAudioFormat wav;
    te::Renderer::Parameters params (*edit);
    params.destFile = output;
    params.audioFormat = &wav;
    params.bitDepth = bitDepth;
    params.sampleRateForAudio = sampleRate;
    params.blockSizeForAudio = 512;
    params.time = te::TimeRange (secondsToTime (0), secondsToTime (endSeconds));
    params.tracksToDo = tracksToDo;
    params.canRenderInMono = false;
    params.usePlugins = true;
    params.useMasterPlugins = false;
    params.checkNodesForAudio = false;

    // レンダリング中はオーディオデバイスから切り離す（終了後に再接続される）
    const te::Edit::ScopedRenderStatus renderStatus (*edit, true);
    output.deleteFile();
    return te::Renderer::renderToFile ("ShareDAW render", params).existsAsFile();
}

juce::Result EngineBridge::renderOneTrack (const std::string& trackId, const juce::File& output, double endSeconds, bool asStem, int bitDepth)
{
    sync();
    stop();

    auto it = bindings.find (trackId);

    if (it == bindings.end())
        return juce::Result::fail ("トラックが見つかりません"_ju);

    if (it->second.renderMode)
        return juce::Result::fail ("この環境ではプラグインを鳴らせないため、バウンスできません"_ju);

    auto& track = *it->second.track;
    const ScopedIsolatedTrack isolated (track, it->second.sends, asStem ? nullptr : it->second.strip, ! asStem);
    const bool ok = renderTracksToWav (tracksMatching ([&track] (te::Track* t) { return t == &track; }),
                                       output, endSeconds, bitDepth, (double) collab::kSampleRate);

    return ok ? juce::Result::ok() : juce::Result::fail ("書き出しに失敗しました"_ju);
}

std::string EngineBridge::trackFingerprint (const collab::Track& t) const
{
    const auto dir = document.getProjectDir();
    return collab::trackSourceFingerprint (t, [dir] (const std::string& ref) { return PluginHost::stateHash (dir, ref); });
}

juce::Result EngineBridge::bounceTrack (const std::string& trackId, collab::Render& result, collab::SampleCount& lengthSamples)
{
    if (! document.hasLocation())
        return juce::Result::fail ("プロジェクトがまだ保存されていません"_ju);

    const auto* track = document.getProject().findTrack (trackId);

    if (track == nullptr)
        return juce::Result::fail ("トラックが見つかりません"_ju);

    const auto fingerprint = trackFingerprint (*track);
    const auto audioDir = document.getProjectDir().getChildFile ("audio");
    audioDir.createDirectory();

    juce::TemporaryFile temp (audioDir.getChildFile ("bounce.wav"));
    const double tailSeconds = tailSecondsFor (trackId);   // リバーブの余韻まで入れる

    if (auto r = renderTrack (trackId, temp.getFile(), tailSeconds); r.failed())
        return r;

    const auto hash = AudioFiles::hashFile (temp.getFile());

    {
        juce::WavAudioFormat wav;

        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (wav.createReaderFor (temp.getFile().createInputStream().release(), true)))
            lengthSamples = (collab::SampleCount) reader->lengthInSamples;
        else
            return juce::Result::fail ("書き出したファイルを読めません"_ju);
    }

    if (hash.empty())
        return juce::Result::fail ("オーディオファイルを読めません"_ju);

    auto target = AudioFiles::newFileForHash (document.getProjectDir(), hash, toJuce (track->name) + "_bounce");

    if (! target.existsAsFile() && ! temp.getFile().moveFileTo (target))
        return juce::Result::fail ("保存できません: "_ju + target.getFullPathName());

    result.audioHash = hash;
    result.renderedAt = collab::nowUtcIso8601();
    result.sourceFingerprint = fingerprint;
    result.tailSeconds = tailSeconds;
    return juce::Result::ok();
}

//==============================================================================
bool EngineBridge::renderToFile (const juce::File& output, collab::Tick endTick, double tailSeconds, int bitDepth, double sampleRate)
{
    sync();
    stop();

    const bool withChords = document.getProject().chordTrack.playback.enabled;
    const auto tracksToDo = tracksMatching ([this, withChords] (te::Track* t)
    {
        return dynamic_cast<te::AudioTrack*> (t) != nullptr && t != metronomeTrack.get() && (t != chordTrack.get() || withChords);
    });

    const double end = document.getTempoMap().tickToSeconds ((double) endTick) + tailSeconds;
    return renderTracksToWav (tracksToDo, output, end, bitDepth, sampleRate);
}
