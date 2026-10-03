// MainComponent の別のウィンドウ・ダイアログ: ピアノロールの画面、ミキサー、エフェクト・EQ/Comp・マスターの画面、設定、ショートカット一覧、クレジット

#include "MainComponent.h"
#include "MainComponentCommands.h"

#include <iostream>
#include "Dialogs.h"
#include "BuiltinEffectEditor.h"
#include "ChannelStripEditor.h"
#include "MarkerLane.h"
#include "MidiInputPanel.h"
#include "MasterPanel.h"
#include "ProjectPicker.h"
#include "MixerView.h"
#include "SyncUI.h"
#include "Theme.h"
#include "collab/ChordPlayback.h"
#include "collab/ClipEditing.h"
#include "collab/MasterDsp.h"
#include "collab/Uuid.h"
#include "audio/Export.h"
#include "audio/Takes.h"
#include "sync/SyncManager.h"

using namespace MainCommands;

void MainComponent::togglePianoFullScreen()
{
    pianoFullScreen = ! pianoFullScreen;

    if (pianoFullScreen)
    {
        // ピアノロールを別のウィンドウ（画面いっぱい）へ移す。ルーラーの下にキー・コード・マーカーの段を小さく出す
        struct Page  : public juce::Component
        {
            explicit Page (juce::Component& c) : content (c)    { addAndMakeVisible (content); }
            void resized() override                             { content.setBounds (getLocalBounds()); }
            void paint (juce::Graphics& g) override             { g.fillAll (Theme::background); }
            juce::Component& content;
        };

        struct Window  : public juce::DocumentWindow
        {
            Window (MainComponent& o)
                : DocumentWindow ("ピアノロール"_ju, Theme::panel, DocumentWindow::closeButton), owner (o) {}

            void closeButtonPressed() override
            {
                // 閉じるのは後で（このウィンドウを消すので、ボタンの処理から抜けてから）
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (&owner)]
                {
                    if (safe != nullptr && safe->pianoFullScreen)
                        safe->togglePianoFullScreen();
                });
            }

            MainComponent& owner;
        };

        pianoLanes = std::make_unique<PianoTopLanes> (ctx);
        pianoRoll.setTopStrip (pianoLanes.get());

        auto window = std::make_unique<Window> (*this);
        window->setUsingNativeTitleBar (true);
        window->setContentOwned (new Page (pianoRoll), false);
        window->setResizable (true, false);
        window->setResizeLimits (400, 300, 6000, 4000);
        window->addKeyListener (&numpadKeys);   // この画面でも Space・E などが効くように

        if (auto* top = getTopLevelComponent())
            if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (top->getScreenBounds()))
                window->setBounds (display->userArea);

        window->setVisible (true);
        window->toFront (true);
        pianoWindow = std::move (window);
        pianoRoll.focusEditor();
    }
    else
    {
        // ピアノロールを元の場所に戻す
        pianoRoll.setTopStrip (nullptr);
        addAndMakeVisible (pianoRoll);
        pianoWindow = nullptr;
        pianoLanes = nullptr;
    }

    transport.setPianoFullScreen (pianoFullScreen);
    resized();
    state.changed();
    commandManager.commandStatusChanged();
}

void MainComponent::toggleMixer()
{
    if (mixerWindow == nullptr)
    {
        struct Window  : public juce::DocumentWindow
        {
            Window (MainComponent& o)
                : DocumentWindow ("ミキサー"_ju, Theme::panel, DocumentWindow::closeButton), owner (o) {}

            void closeButtonPressed() override
            {
                setVisible (false);
                owner.commandManager.commandStatusChanged();
            }

            MainComponent& owner;
        };

        auto window = std::make_unique<Window> (*this);
        window->setUsingNativeTitleBar (true);
        window->setContentOwned (new MixerView (ctx), true);
        window->setResizable (true, false);
        window->setResizeLimits (300, 280, 4000, 2000);
        window->addKeyListener (&numpadKeys);   // ミキサーの上でも F3 などが効くように

        // 最初は画面いっぱい（メイン画面のあるディスプレイの作業領域）
        if (auto* top = getTopLevelComponent())
            if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (top->getScreenBounds()))
                window->setBounds (display->userArea);

        mixerWindow = std::move (window);
    }

    mixerWindow->setVisible (! mixerWindow->isVisible());

    if (mixerWindow->isVisible())
        mixerWindow->toFront (true);

    commandManager.commandStatusChanged();
}

bool MainComponent::openBuiltinEffect (const std::string& trackId, const std::string& effectId)
{
    auto* t = document.getProject().findTrack (trackId);
    const collab::Effect* effect = nullptr;

    if (t != nullptr)
        for (auto& e : t->effects)
            if (e.id == effectId && e.isBuiltin())
                effect = &e;

    if (effect == nullptr)
        return false;

    // エフェクトごとに 1 つのウィンドウ
    auto& slot = effectWindows[effectId];

    if (slot == nullptr)
    {
        struct Window  : public juce::DocumentWindow
        {
            Window() : DocumentWindow ("Effect", Theme::panel, DocumentWindow::closeButton) {}
            void closeButtonPressed() override      { setVisible (false); }
        };

        auto window = std::make_unique<Window>();
        window->setUsingNativeTitleBar (true);
        auto* editor = new BuiltinEffectEditor (ctx, trackId, effectId);
        window->setName (editor->getTitle());
        window->setContentOwned (editor, true);
        window->setResizable (false, false);
        window->addKeyListener (&numpadKeys);

        if (auto* top = getTopLevelComponent())
            window->setTopLeftPosition (top->getX() + 180 + 24 * (int) (effectWindows.size() % 6), top->getY() + 180 + 24 * (int) (effectWindows.size() % 6));

        slot = std::move (window);
    }

    slot->setVisible (true);
    slot->toFront (true);
    return true;
}

void MainComponent::openChannelStrip (const std::string& trackId, bool compressor)
{
    if (document.getProject().findTrack (trackId) == nullptr)
        return;

    // EQ と Compressor で 1 つずつウィンドウを使い回し、開くトラックを切り替える
    auto& slot = compressor ? compWindow : eqWindow;

    if (slot == nullptr)
    {
        struct Window  : public juce::DocumentWindow
        {
            Window() : DocumentWindow ("EQ", Theme::panel, DocumentWindow::closeButton) {}
            void closeButtonPressed() override      { setVisible (false); }
        };

        auto window = std::make_unique<Window>();
        window->setUsingNativeTitleBar (true);
        auto* editor = new ChannelStripEditor (ctx, trackId, compressor ? ChannelStripEditor::Section::comp : ChannelStripEditor::Section::eq);
        auto* w = window.get();
        editor->onTitleChanged = [w, editor] { w->setName (editor->getTitle()); };
        window->setContentOwned (editor, true);
        window->setResizable (false, false);
        window->addKeyListener (&numpadKeys);

        if (auto* top = getTopLevelComponent())
            window->setTopLeftPosition (top->getX() + (compressor ? 160 : 120), top->getY() + (compressor ? 160 : 120));

        slot = std::move (window);
    }

    if (auto* editor = dynamic_cast<ChannelStripEditor*> (slot->getContentComponent()))
    {
        editor->setTrack (trackId);
        slot->setName (editor->getTitle());
    }

    slot->setVisible (true);
    slot->toFront (true);
}

void MainComponent::openMaster()
{
    if (masterWindow == nullptr)
    {
        struct Window  : public juce::DocumentWindow
        {
            Window (MainComponent& o) : DocumentWindow ("マスター - Vintage Limiter / Loudness"_ju, Theme::panel, DocumentWindow::closeButton), owner (o) {}

            void closeButtonPressed() override
            {
                setVisible (false);
                owner.commandManager.commandStatusChanged();
            }

            MainComponent& owner;
        };

        auto window = std::make_unique<Window> (*this);
        window->setUsingNativeTitleBar (true);
        window->setContentOwned (new MasterPanel (ctx), true);
        window->setResizable (false, false);
        window->addKeyListener (&numpadKeys);

        if (auto* top = getTopLevelComponent())
            window->setTopLeftPosition (top->getX() + 160, top->getY() + 140);

        masterWindow = std::move (window);
    }

    // F4 で開く・閉じる
    const bool show = ! masterWindow->isVisible() || ! masterWindow->isActiveWindow();
    masterWindow->setVisible (show);

    if (show)
        masterWindow->toFront (true);

    commandManager.commandStatusChanged();
}

void MainComponent::showShortcuts()
{
    const juce::String text (
        "■ ツール（Cubase と同じ番号。テンキーでも可）\n"_ju
        "  1 選択 / 2 鉛筆 / 3 はさみ\n"_ju
        "  空いている所を右クリックでツールの切り替え・貼り付け・トラックの追加\n"_ju
        "\n"_ju
        "■ クリップ（タイムライン）\n"_ju
        "  クリック: 選択　Ctrl/Shift+クリック: 選択に追加・解除　空いている所をドラッグ: 範囲選択\n"_ju
        "  ドラッグ: 移動（複数でもまとめて）　左端・右端をドラッグ: 長さ（MIDI・オーディオとも）\n"_ju
        "  Alt を押しながら: スナップを一時的に解除\n"_ju
        "  Ctrl+C / X / V: コピー / 切り取り / 貼り付け（再生位置へ）　Ctrl+D: 複製　Delete: 削除\n"_ju
        "  Ctrl+← / →: クオンタイズ値ずつずらす　Alt+X: 再生位置で分割\n"_ju
        "  M / S: 選択中のトラックのミュート / ソロ\n"_ju
        "  鉛筆: 空いている所をクリック（ドラッグで長さ）で MIDI クリップを作成　ダブルクリック: ピアノロールで開く\n"_ju
        "\n"_ju
        "■ ピアノロール\n"_ju
        "  鉛筆: クリックで追加（ドラッグで長さ）、ノートをクリックで削除\n"_ju
        "  選択: ドラッグで移動・範囲選択、右端で長さ　↑↓: 半音　Shift/Ctrl+↑↓: オクターブ　←→: クオンタイズ値ずつ\n"_ju
        "  はさみ: クリック位置でノートを分割\n"_ju
        "  Ctrl+C / X / V / D: コピー / 切り取り / 貼り付け / 複製　Q: クオンタイズ　Ctrl+A: すべて選択\n"_ju
        "\n"_ju
        "■ 再生・録音\n"_ju
        "  Space: 再生／停止　テンキー 0: 停止（停止中なら先頭へ）　Home / テンキー .: 先頭へ\n"_ju
        "  テンキー + / -: 1 小節進む / 戻る　R / テンキー *: 録音　L / テンキー /: ループ　P: 選択範囲をループ範囲に\n"_ju
        "  C: メトロノーム　F: 自動スクロール　J: スナップ　G / H: 縮小 / 拡大\n"_ju
        "\n"_ju
        "■ マーカー\n"_ju
        "  Insert: 再生位置に追加　Shift+1〜9: マーカーへ移動　ダブルクリック: 名前\n"_ju
        "\n"_ju
        "■ そのほか\n"_ju
        "  F3: ミキサー　F4: マスター（リミッター / ラウドネス）　F7: 同期パネル　Ctrl+Z / Ctrl+Shift+Z: 元に戻す / やり直し　Ctrl+S: 保存　Ctrl+I: オーディオを読み込む\n"_ju
        "  BPM・拍子: トランスポートバーの数字をクリックして入力、ホイールで増減\n"_ju);

    auto editor = std::make_unique<juce::TextEditor>();
    editor->setMultiLine (true);
    editor->setReadOnly (true);
    editor->setScrollbarsShown (true);
    editor->setFont (juce::FontOptions (15.5f));
    editor->setColour (juce::TextEditor::backgroundColourId, Theme::background);
    editor->setColour (juce::TextEditor::textColourId, Theme::text);
    editor->setText (text, false);
    editor->setSize (760, 560);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (editor.release());
    o.dialogTitle = "操作とショートカット"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::showPluginManager()
{
    struct Manager  : public juce::Component
    {
        Manager (te::Engine& e, juce::PropertiesFile& props)
            : engine (e),
              list (e.getPluginManager().pluginFormatManager, e.getPluginManager().knownPluginList,
                    e.getTemporaryFileManager().getTempDirectory().getChildFile ("plugin-scan-dead-mans-pedal"), &props, true)
        {
            scanButton.setButtonText ("プラグインをスキャン"_ju);
            scanButton.onClick = [this]
            {
                PluginHost::ScanResult result;
                SyncUI::runWithProgress ("プラグインをスキャンしています（別プロセス）"_ju, [&]
                {
                    result = PluginHost::scan (engine, nullptr);
                    return juce::Result::ok();
                });

                juce::String text = juce::String (result.found) + " 個のプラグインがあります。"_ju;

                if (! result.blacklisted.isEmpty())
                    text << "\n" << "スキャン中に問題が起きたため、次のプラグインは読み込みません: "_ju
                         << result.blacklisted.joinIntoString ("、"_ju);

                info.setText (text, juce::dontSendNotification);
            };

            info.setText ("VST3（Windows / Mac）と AU（Mac）に対応しています。スキャンは別プロセスで行います。"_ju, juce::dontSendNotification);
            info.setColour (juce::Label::textColourId, Theme::textDim);
            addAndMakeVisible (scanButton);
            addAndMakeVisible (info);
            addAndMakeVisible (list);
            setSize (640, 480);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            auto top = r.removeFromTop (30);
            scanButton.setBounds (top.removeFromLeft (180));
            top.removeFromLeft (8);
            info.setBounds (top);
            r.removeFromTop (6);
            list.setBounds (r);
        }

        te::Engine& engine;
        juce::TextButton scanButton;
        juce::Label info;
        juce::PluginListComponent list;
    };

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new Manager (engine, settings));
    o.dialogTitle = "プラグイン"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::showAudioSettings()
{
    auto& dm = engine.getDeviceManager().deviceManager;

    // MIDI 入力は Tracktion が管理するので、JUCE の一覧ではなく下の「MIDI キーボード」欄で切り替える。
    // 入出力は機器のチャンネルをすべて使えるようにする（Babyface Pro FS など多チャンネルの機器。入力はモノラルごと、出力はステレオの組）
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (dm, 0, 256, 0, 256, false, false, false, false);
    auto note = std::make_unique<juce::Label>();
    note->setText ("サンプルレートは 48000 Hz 推奨。Windows で多チャンネルの機器は ASIO を選ぶ"_ju, juce::dontSendNotification);
    note->setFont (juce::FontOptions (15.0f));
    note->setColour (juce::Label::textColourId, Theme::textDim);

    selector->setBounds (0, 0, 560, 520);
    note->setBounds (8, 524, 544, 24);

    // マスター（曲の音）とメトロノームを出す出力（ステレオの組）
    struct MasterOut  : public juce::Component,
                        private juce::ChangeListener
    {
        explicit MasterOut (te::DeviceManager& d) : dm (d)
        {
            title.setText ("マスター出力"_ju, juce::dontSendNotification);
            title.setFont (juce::FontOptions (15.0f));
            addAndMakeVisible (title);
            box.onChange = [this]
            {
                const int i = box.getSelectedItemIndex();

                if (i >= 0 && i < ids.size() && ids[i] != dm.getDefaultWaveOutDeviceID())
                    dm.setDefaultWaveOutDevice (ids[i]);
            };
            addAndMakeVisible (box);
            dm.addChangeListener (this);
            refresh();
        }

        ~MasterOut() override   { dm.removeChangeListener (this); }

        void refresh()
        {
            box.clear (juce::dontSendNotification);
            ids.clear();

            for (int i = 0; i < dm.getNumWaveOutDevices(); ++i)
                if (auto* d = dm.getWaveOutDevice (i); d != nullptr && d->isEnabled())
                {
                    ids.add (d->getDeviceID());
                    box.addItem (d->getName(), ids.size());

                    if (d->getDeviceID() == dm.getDefaultWaveOutDeviceID())
                        box.setSelectedId (ids.size(), juce::dontSendNotification);
                }
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override    { refresh(); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 0);
            title.setBounds (r.removeFromLeft (150));
            box.setBounds (r.reduced (0, 2));
        }

        te::DeviceManager& dm;
        juce::Label title;
        juce::ComboBox box;
        juce::StringArray ids;
    };

    // レイテンシ補正（§3.5）: ドライバが報告する値で自動補正し、さらにデバイスごとに手動でずらせる
    struct Latency  : public juce::Component,
                      private juce::ChangeListener
    {
        Latency (MainComponent& o) : owner (o)
        {
            title.setText ("録音のレイテンシ補正（手動、サンプル）"_ju, juce::dontSendNotification);
            title.setFont (juce::FontOptions (14.5f));
            addAndMakeVisible (title);

            offset.setRange (-2000, 2000, 1);
            offset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
            offset.setDoubleClickReturnValue (true, 0);
            offset.onValueChange = [this]
            {
                if (auto key = owner.latencySettingKey(); key.isNotEmpty())
                    owner.settings.setValue (key, (int) offset.getValue());

                owner.applyLatencyOffset();
            };
            addAndMakeVisible (offset);

            midiTitle.setText ("MIDI の録音位置の補正（ms）"_ju, juce::dontSendNotification);
            midiTitle.setFont (juce::FontOptions (14.5f));
            addAndMakeVisible (midiTitle);

            midiOffset.setRange (-200, 200, 1);
            midiOffset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
            midiOffset.setDoubleClickReturnValue (true, 0);
            midiOffset.setValue (owner.settings.getDoubleValue ("midiRecordOffsetMs", 0.0), juce::dontSendNotification);
            midiOffset.onValueChange = [this]
            {
                owner.settings.setValue ("midiRecordOffsetMs", midiOffset.getValue());
                owner.applyLatencyOffset();
            };
            addAndMakeVisible (midiOffset);

            info.setFont (juce::FontOptions (15.0f));
            info.setColour (juce::Label::textColourId, Theme::textDim);
            addAndMakeVisible (info);

            owner.engine.getDeviceManager().deviceManager.addChangeListener (this);
            update();
        }

        ~Latency() override
        {
            owner.engine.getDeviceManager().deviceManager.removeChangeListener (this);
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override    { update(); }

        void update()
        {
            auto* device = owner.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
            const auto key = owner.latencySettingKey();
            offset.setValue (key.isEmpty() ? 0 : owner.settings.getIntValue (key, 0), juce::dontSendNotification);
            offset.setEnabled (device != nullptr);

            if (device != nullptr)
            {
                const double rate = juce::jmax (1.0, device->getCurrentSampleRate());
                const int in = device->getInputLatencyInSamples(), out = device->getOutputLatencyInSamples();
                info.setText ("オーディオはドライバが報告する遅れ（入力 "_ju + juce::String (in) + " + 出力 "_ju + juce::String (out)
                                + " サンプル = "_ju + juce::String ((in + out) * 1000.0 / rate, 1) + " ms）の分だけ、録音を自動で前にずらしています。"_ju
                                + "録音が前のめり（早い）なら負の値、遅れるなら正の値にしてください（どちらも、正の値で録音を前＝早くずらします）。"_ju,
                              juce::dontSendNotification);
            }
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (8, 0);
            auto row = area.removeFromTop (26);
            title.setBounds (row.removeFromLeft (260));
            offset.setBounds (row);
            auto midiRow = area.removeFromTop (26);
            midiTitle.setBounds (midiRow.removeFromLeft (260));
            midiOffset.setBounds (midiRow);
            info.setBounds (area);
        }

        MainComponent& owner;
        juce::Label title, info, midiTitle;
        juce::Slider offset { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Slider midiOffset { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    };

    auto latency = std::make_unique<Latency> (*this);
    auto masterOut = std::make_unique<MasterOut> (engine.getDeviceManager());
    masterOut->setBounds (0, 552, 560, 32);
    latency->setBounds (0, 590, 560, 110);

    struct Holder : juce::Component
    {
        std::unique_ptr<juce::Component> a, b, c, d, e;
    };

    auto holder = std::make_unique<Holder>();
    holder->a = std::move (selector);
    holder->b = std::move (note);
    holder->c = std::move (latency);
    holder->e = std::move (masterOut);
    holder->addAndMakeVisible (*holder->a);
    holder->addAndMakeVisible (*holder->b);
    holder->addAndMakeVisible (*holder->c);
    holder->addAndMakeVisible (*holder->e);

    const int midiHeight = 60 + juce::jmax (1, (int) bridge.getMidiInputs().size()) * 26 + 26;
    holder->d = std::make_unique<MidiInputPanel> (bridge);
    holder->d->setBounds (0, 706, 560, midiHeight);
    holder->addAndMakeVisible (*holder->d);
    holder->setSize (560, 706 + midiHeight);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (holder.release());
    o.dialogTitle = "オーディオ・MIDI の設定"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::showCredits()
{
    juce::String text = "ShareDAW はオープンソースの JUCE / Tracktion Engine（GPL）、sfizz（BSD-2-Clause）を使用しています。\n\n"_ju
                        "内蔵音源:\n"_ju;

    for (auto* m : library.getAll())
    {
        text << "- " << toJuce (m->displayName) << " (" << toJuce (m->id) << " " << toJuce (m->version) << ")\n";

        for (auto& c : m->credits)
            text << "    " << toJuce (c) << "\n";
    }

    Dialogs::showInfo ("クレジット"_ju, text);
}
