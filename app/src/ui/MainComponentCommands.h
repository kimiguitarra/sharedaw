#pragma once

// MainComponent のコマンド ID（MainComponent*.cpp の中だけで使う）

namespace MainCommands
{
    enum Commands : int
    {
        cmdNew = 0x2000, cmdOpen, cmdSave, cmdUndo, cmdRedo, cmdDelete, cmdSelectAll, cmdDuplicate,
        cmdPlay, cmdToStart, cmdLoop, cmdMetronome, cmdQuantise,
        cmdAddDrums, cmdAddBass, cmdAddPiano, cmdAddEPiano,
        cmdAudioSettings, cmdCredits, cmdAbout, cmdCheckUpdate,
        cmdFont100, cmdFont125, cmdFont150, cmdFont175, cmdFont200,
        cmdSyncSettings, cmdSyncRegister, cmdSyncOpen, cmdSyncPull, cmdSyncPush, cmdSyncHistory,
        cmdAddAudioTrack, cmdImportAudio, cmdImportMidi, cmdExportMixdown, cmdExportMp3, cmdExportStems, cmdExportMidi, cmdSplit, cmdMuteTrack, cmdSoloTrack, cmdPlugins, cmdArmTrack, cmdTrackHeight,
        cmdRecord, cmdCountIn0, cmdCountIn1, cmdCountIn2,
        cmdToolSelect, cmdToolPencil, cmdModeCubase, cmdModeStudioOne, cmdMixer, cmdMaster, cmdLoopToSelection,
        cmdStop, cmdZoomIn, cmdZoomOut, cmdSnap, cmdAutoScroll, cmdAddMarker,
        cmdMarker1, cmdMarker2, cmdMarker3, cmdMarker4, cmdMarker5, cmdMarker6, cmdMarker7, cmdMarker8, cmdMarker9,
        cmdToolSplit, cmdCopy, cmdCut, cmdPaste, cmdNudgeLeft, cmdNudgeRight,
        cmdForward, cmdRewind, cmdShortcuts, cmdSyncPanel, cmdSyncCreate, cmdToLoopStart, cmdToLoopEnd, cmdInspector, cmdCursorLeft, cmdCursorRight, cmdBarLeft, cmdBarRight, cmdTrackUp, cmdTrackDown, cmdPianoFull, cmdWaveBigger, cmdWaveSmaller, cmdAutoArm
    };

    inline constexpr float fontScales[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
}
