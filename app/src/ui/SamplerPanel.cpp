#include "SamplerPanel.h"

#include "Dialogs.h"
#include "Theme.h"
#include "audio/AudioFiles.h"
#include "collab/ClipEditing.h"
#include "collab/GmDrumMap.h"

SamplerPanel::SamplerPanel (AppContext& c, std::string id) : ctx (c), trackId (std::move (id))
{
    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    volume.setRange (-40.0, 12.0, 0.1);
    volume.setTextValueSuffix (" dB");
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.onDragStart = [this] { mergeId = juce::Uuid().toString(); };
    volume.onDragEnd = [this] { ctx.document.endMerge(); mergeId = {}; };
    volume.onValueChange = [this]
    {
        const double v = volume.getValue();
        ctx.editTrack (trackId, "サンプラーの音量"_ju, [v] (collab::Track& t)
        {
            if (t.instrument)
                t.instrument->params["volumeDb"] = v;
        }, mergeId);
    };
    addAndMakeVisible (volume);
    addAndMakeVisible (volumeLabel);

    ctx.document.addChangeListener (this);
    setSize (680, 600);
    changeListenerCallback (nullptr);
}

SamplerPanel::~SamplerPanel()
{
    ctx.document.removeChangeListener (this);
}

void SamplerPanel::setTrack (std::string id)
{
    trackId = std::move (id);
    changeListenerCallback (nullptr);
}

const collab::Track* SamplerPanel::track() const
{
    return ctx.document.getProject().findTrack (trackId);
}

std::vector<collab::SamplerPad> SamplerPanel::pads() const
{
    auto* t = track();
    return collab::samplerPads (t != nullptr && t->instrument ? t->instrument->params : nlohmann::json::object());
}

juce::String SamplerPanel::getTitle() const
{
    auto* t = track();
    return (t != nullptr ? toJuce (t->name) + " - " : juce::String()) + "サンプラー"_ju;
}

void SamplerPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto* t = track(); t != nullptr && t->instrument)
        volume.setValue (t->instrument->params.value ("volumeDb", 0.0), juce::dontSendNotification);

    if (onTitleChanged)
        onTitleChanged();

    repaint();
}

juce::Rectangle<int> SamplerPanel::padBounds (int index) const
{
    // 左下がパッド 1、右へ 2・3・4、その上の段が 5〜8（MPC と同じ並び）
    const int col = index % 4, row = 3 - index / 4;
    const int w = padsArea.getWidth() / 4, h = padsArea.getHeight() / 4;
    return juce::Rectangle<int> (padsArea.getX() + col * w, padsArea.getY() + row * h, w, h).reduced (5);
}

int SamplerPanel::padAt (juce::Point<int> p) const
{
    for (int i = 0; i < collab::kSamplerPads; ++i)
        if (padBounds (i).contains (p))
            return i;

    return -1;
}

void SamplerPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    auto* t = track();
    const auto colour = t != nullptr ? Theme::parseColour (t->color) : Theme::accent;
    const auto list = pads();

    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto& pad = list[(size_t) i];
        const auto r = padBounds (i).toFloat();
        const bool filled = ! pad.audioHash.empty();

        g.setColour (filled ? colour.withAlpha (i == pressedPad ? 0.55f : 0.3f) : Theme::panelLight.darker (0.06f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (i == dropPad ? Theme::selection : filled ? colour.darker (0.2f) : Theme::overlay (0.18f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, i == dropPad ? 2.5f : 1.0f);

        auto inner = r.reduced (8.0f, 6.0f);
        auto top = inner.removeFromTop (18.0f);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), top, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (toJuce (collab::midiNoteName (pad.note)), top, juce::Justification::centredRight);

        if (! filled)
        {
            g.setFont (juce::FontOptions (12.5f));
            g.drawText ("空き"_ju, inner, juce::Justification::centred, true);
            continue;
        }

        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (toJuce (pad.name), inner.removeFromTop (20.0f), juce::Justification::centredLeft, true);

        // 設定（変えているものだけ）
        juce::StringArray info;

        if (std::abs (pad.gainDb) > 0.05)         info.add (juce::String (pad.gainDb, 1) + " dB");
        if (std::abs (pad.tuneSemitones) > 0.01)  info.add ((pad.tuneSemitones > 0 ? "+" : "") + juce::String (pad.tuneSemitones, 1) + " st");
        if (! pad.oneShot)                        info.add ("離すと止める"_ju);
        if (pad.chokeGroup > 0)                   info.add ("チョーク "_ju + juce::String (pad.chokeGroup));
        if (pad.sourceBpm > 0.0)                  info.add (juce::String (pad.sourceBpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " BPM");

        auto infoRow = inner.removeFromBottom (16.0f);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (info.joinIntoString ("  "), infoRow, juce::Justification::centredLeft, true);

        // 波形
        if (auto* thumb = ctx.audioCache.getThumbnail (ctx.document.getProjectDir(), pad.audioHash); thumb != nullptr && thumb->getTotalLength() > 0.0)
            AudioFiles::drawWaveform (g, *thumb, inner.reduced (0.0f, 2.0f), 0.0, thumb->getTotalLength(),
                                      juce::Decibels::decibelsToGain ((float) pad.gainDb), Theme::clipWave (colour));
        else if (! ctx.document.hasLocation() || ! AudioFiles::fileForHash (ctx.document.getProjectDir(), pad.audioHash).existsAsFile())
        {
            g.setColour (Theme::warning);
            g.drawText ("オーディオがありません（同期でダウンロード）"_ju, inner, juce::Justification::centred, true);
        }
    }
}

void SamplerPanel::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto top = area.removeFromTop (28);
    volumeLabel.setBounds (top.removeFromLeft (44));
    volume.setBounds (top.removeFromLeft (280));
    area.removeFromTop (8);
    padsArea = area;
}

void SamplerPanel::mouseDown (const juce::MouseEvent& e)
{
    const int index = padAt (e.getPosition());

    if (index < 0)
        return;

    if (e.mods.isPopupMenu())
        return showPadMenu (index);

    // 押したら試し弾き（ベロシティは押した高さで: 上ほど強い）
    const auto r = padBounds (index);
    const int velocity = juce::jlimit (30, 127, 127 - (int) (60.0f * (float) (e.y - r.getY()) / (float) juce::jmax (1, r.getHeight())));
    ctx.engine.previewNote (trackId, pads()[(size_t) index].note, velocity);
    pressedPad = index;
    repaint();
    juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<SamplerPanel> (this)] { if (safe != nullptr) { safe->pressedPad = -1; safe->repaint(); } });
}

void SamplerPanel::editPads (const juce::String& description, std::function<void (std::vector<collab::SamplerPad>&)> fn)
{
    ctx.editTrack (trackId, description, [fn] (collab::Track& t)
    {
        if (! t.instrument)
            return;

        auto list = collab::samplerPads (t.instrument->params);
        fn (list);
        t.instrument->params = collab::withSamplerPads (t.instrument->params, list);
    }, {});
}

bool SamplerPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a"))
            return true;

    return false;
}

void SamplerPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int index = padAt ({ x, y });
    dropPad = -1;
    repaint();

    if (index >= 0)
        assignFiles (index, files);
}

void SamplerPanel::assignFiles (int firstPad, const juce::StringArray& files)
{
    if (! ctx.document.hasLocation())
        return Dialogs::showInfo ("サンプラー"_ju, "オーディオは曲のフォルダに保存するので、先に曲を保存してください。"_ju);

    // 複数のファイルは、そのパッドから順に入れる
    std::vector<std::pair<int, AudioFiles::Imported>> imported;
    juce::StringArray errors;
    int index = firstPad;

    for (auto& path : files)
    {
        if (index >= collab::kSamplerPads)
            break;

        const juce::File f (path);
        AudioFiles::Imported im;

        if (auto r = AudioFiles::importFile (f, ctx.document.getProjectDir().getChildFile ("audio"), im, f.getFileNameWithoutExtension()); r.failed())
        {
            errors.add (r.getErrorMessage());
            continue;
        }

        im.displayName = f.getFileNameWithoutExtension();
        imported.push_back ({ index++, im });
    }

    if (! errors.isEmpty())
        Dialogs::showError ("読み込めないファイルがありました"_ju, errors.joinIntoString ("\n"));

    if (imported.empty())
        return;

    editPads ("サンプラーにオーディオを入れる"_ju, [imported] (std::vector<collab::SamplerPad>& list)
    {
        for (auto& [i, im] : imported)
        {
            auto& pad = list[(size_t) i];
            pad.audioHash = im.hash;
            pad.name = toStd (im.displayName);
        }
    });
}

void SamplerPanel::showPadMenu (int index)
{
    const auto pad = pads()[(size_t) index];
    juce::PopupMenu m;

    m.addItem ("オーディオを読み込む…"_ju, [this, index]
    {
        chooser = std::make_unique<juce::FileChooser> ("オーディオを読み込む"_ju, juce::File(), AudioFiles::supportedWildcard());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this, index] (const juce::FileChooser& fc)
        {
            juce::StringArray paths;

            for (auto& f : fc.getResults())
                paths.add (f.getFullPathName());

            if (! paths.isEmpty())
                assignFiles (index, paths);
        });
    });

    // ノート（オクターブごと）
    juce::PopupMenu notes;

    for (int octave = 0; octave < 9; ++octave)
    {
        juce::PopupMenu sub;

        for (int n = octave * 12 + 12; n < octave * 12 + 24 && n < 128; ++n)
            sub.addItem (toJuce (collab::midiNoteName (n)), true, n == pad.note, [this, index, n]
            {
                editPads ("パッドのノート"_ju, [index, n] (auto& list) { list[(size_t) index].note = n; });
            });

        notes.addSubMenu (toJuce (collab::midiNoteName (octave * 12 + 12)) + " 〜"_ju, sub);
    }

    m.addSubMenu ("ノート（"_ju + toJuce (collab::midiNoteName (pad.note)) + "）"_ju, notes);

    juce::PopupMenu pitch;

    for (int st = 12; st >= -12; --st)
        pitch.addItem ((st > 0 ? "+" : "") + juce::String (st), true, std::abs (pad.tuneSemitones - st) < 0.01, [this, index, st]
        {
            editPads ("パッドのピッチ"_ju, [index, st] (auto& list) { list[(size_t) index].tuneSemitones = st; });
        });

    m.addSubMenu ("ピッチ（半音）"_ju, pitch, ! pad.audioHash.empty());

    m.addItem ("音量…"_ju, ! pad.audioHash.empty(), false, [this, index, pad]
    {
        Dialogs::askText ("パッドの音量"_ju, "dB（例: -3）"_ju, juce::String (pad.gainDb, 1), [this, index] (const juce::String& text)
        {
            const double db = juce::jlimit (-60.0, 24.0, text.getDoubleValue());
            editPads ("パッドの音量"_ju, [index, db] (auto& list) { list[(size_t) index].gainDb = db; });
        });
    });

    m.addItem ("パン…"_ju, ! pad.audioHash.empty(), false, [this, index, pad]
    {
        Dialogs::askText ("パッドのパン"_ju, "L100〜C〜R100（例: L30）"_ju,
                          pad.pan == 0.0 ? juce::String ("C") : (pad.pan < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (pad.pan) * 100.0)),
                          [this, index] (const juce::String& text)
        {
            const auto t = text.trim().toUpperCase();
            double v = t.startsWith ("L") ? -t.substring (1).getDoubleValue() : t.startsWith ("R") ? t.substring (1).getDoubleValue() : t.getDoubleValue();
            v = juce::jlimit (-1.0, 1.0, v / 100.0);
            editPads ("パッドのパン"_ju, [index, v] (auto& list) { list[(size_t) index].pan = v; });
        });
    });

    juce::PopupMenu mode;
    mode.addItem ("最後まで鳴らす"_ju, true, pad.oneShot, [this, index] { editPads ("パッドの鳴らし方"_ju, [index] (auto& list) { list[(size_t) index].oneShot = true; }); });
    mode.addItem ("離したら止める"_ju, true, ! pad.oneShot, [this, index] { editPads ("パッドの鳴らし方"_ju, [index] (auto& list) { list[(size_t) index].oneShot = false; }); });
    m.addSubMenu ("鳴らし方"_ju, mode, ! pad.audioHash.empty());

    juce::PopupMenu choke;

    for (int group = 0; group <= 4; ++group)
        choke.addItem (group == 0 ? "なし"_ju : juce::String (group), true, pad.chokeGroup == group, [this, index, group]
        {
            editPads ("パッドのチョーク"_ju, [index, group] (auto& list) { list[(size_t) index].chokeGroup = group; });
        });

    m.addSubMenu ("チョーク（同じ番号の音を止める）"_ju, choke, ! pad.audioHash.empty());
    m.addSeparator();

    // テンポ合わせ（Splice などのループを曲のテンポで鳴らす）。素材のテンポは名前か長さから推測し、違えば直せる
    m.addItem ("曲のテンポに合わせる"_ju, ! pad.audioHash.empty(), pad.sourceBpm > 0.0, [this, index, pad]
    {
        if (pad.sourceBpm > 0.0)
        {
            editPads ("パッドのテンポ合わせ"_ju, [index] (auto& list) { list[(size_t) index].sourceBpm = 0.0; });
            return;
        }

        const auto length = ctx.audioCache.getLengthSamples (ctx.document.getProjectDir(), pad.audioHash);
        const double bpm = collab::guessSourceBpm (pad.name, (double) length / collab::kSampleRate, ctx.document.getTempoMap().bpmAtTick (0));
        editPads ("パッドのテンポ合わせ"_ju, [index, bpm] (auto& list) { list[(size_t) index].sourceBpm = bpm; });
    });

    m.addItem ("元のテンポ…"_ju, pad.sourceBpm > 0.0, false, [this, index, pad]
    {
        Dialogs::askText ("元のテンポ"_ju, "BPM（例: 100）"_ju, juce::String (pad.sourceBpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."),
                          [this, index] (const juce::String& text)
        {
            if (const double bpm = text.getDoubleValue(); bpm > 0.0)
                editPads ("パッドのテンポ合わせ"_ju, [index, bpm = juce::jlimit (20.0, 400.0, bpm)] (auto& list) { list[(size_t) index].sourceBpm = bpm; });
        });
    });

    m.addSeparator();
    m.addItem ("空にする"_ju, ! pad.audioHash.empty(), false, [this, index]
    {
        editPads ("パッドを空にする"_ju, [index] (auto& list)
        {
            const int note = list[(size_t) index].note;
            list[(size_t) index] = {};
            list[(size_t) index].note = note;
        });
    });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}
