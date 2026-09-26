#pragma once

#include "EngineBridge.h"
#include "InstrumentLibrary.h"
#include "ProjectDocument.h"
#include "ui/EditorState.h"

/** UI の各部品が共有する参照。 */
struct AppContext
{
    ProjectDocument& document;
    EditorState& state;
    EngineBridge& engine;
    const InstrumentLibrary& library;

    /** 選択中のトラック・クリップ（無ければ nullptr）。 */
    const collab::Track* selectedTrack() const      { return document.getProject().findTrack (state.selectedTrackId); }

    const collab::MidiClip* selectedClip() const
    {
        auto* t = selectedTrack();
        return t != nullptr ? t->findMidiClip (state.selectedClipId) : nullptr;
    }

    /** 新しい MIDI トラック（内蔵音源）を追加して選択する。 */
    void addBuiltinMidiTrack (const std::string& instrumentId, const juce::String& name);
};
