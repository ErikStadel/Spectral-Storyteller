## Plan: Spectral Storyteller - Entwicklungsplan

TL;DR
JUCE-basiertes Audio-Plugin mit Spektral-Objekterkennung und Story-Timeline für zeitliche Soundentwicklung. 8 PRs in 3 Phasen: (1) STFT-Engine + Visualisierung, (2) Masken + Timeline, (3) Auto-Detection + Morphing, (4) Modulation + UI. MVP-Fokus: Funktionales Audio, minimales aber funktionales UI, Plug-and-Play in DAW.

**Steps**

1. PR 1: JUCE-Setup, STFT/iFFT (2048, 75% Overlap), Dry/Wet-Pass. Test: Wet == Input.
2. PR 2: SpectralFrameBuffer, Magnitude/Phase, Custom Spektrogramm-Component. Test: Live-Update ≥30fps.
3. PR 3: ObjectDatabase + Bin-Masken, Lasso-Auswahl, Solo/Mute. Test: Selektion → Mute → hörbar.
4. PR 4: ValueTreeState, Timeline + Keyframes, Linear Interpolation. Test: Keyframe-Automation sync zur DAW.
5. PR 5: Spectral Features (Flux, Tonality, Centroid), K-Means Clustering, Auto-Detect Button. Test: Drum-Loop → 3–5 Objektgruppen.
6. PR 6: Phase-Vocoder, S-Curve Interpolation, Transform/Cross-Synthesis, Preserve-Logik. Test: Pitch-Shift ±12ST ohne Artefakte.
7. PR 7: Modulations-Matrix, LFOs, Envelopes, Audio-Follower, Spectral Tracker. Test: Flux moduliert Brightness hörbar.
8. PR 8: UI-Inspektor, Sidechain-Input, Preset-Browser, Global Controls, Responsive Layout. Test: Vollständiger Workflow, Presets speichern/laden.

**Relevant files**
- Source/PluginProcessor.h/cpp — AudioProcessor, STFT-Engine, DSP-Loop
- Source/DSP/SpectralAnalyzer.h/cpp — FFT, Features, Clustering
- Source/DSP/ObjectDatabase.h/cpp — Objekt-Verwaltung, Masken
- Source/DSP/TimelineEngine.h/cpp — Keyframes, Interpolation
- Source/DSP/PhaseVocoder.h/cpp — Pitch/Formant-Shifting
- Source/DSP/ModulationMatrix.h/cpp — LFO, Envelope, Tracker
- Source/UI/PluginEditor.h/cpp — Main Editor, 4-Spalten-Layout
- Source/UI/SpectralView.h/cpp — Custom Component für Spektrogramm
- Source/UI/TimelineComponent.h/cpp — Timeline mit Keyframes
- Source/UI/ObjectSidebar.h/cpp — Layer-Liste
- Source/UI/InspectorPanel.h/cpp — Parameter-Schieberegler
- CMakeLists.txt / Projucer/ProjucerSettings — JUCE-Projektkonfiguration

**Verification**
1. Jedes PR: Unit-Tests (FFT-Identität, Interpolation), Integration-Test (DAW-Plugin-Host Validator).
2. Finale Validierung: Vollständiger Workflow in Reaper/Logic/Ableton (Stereo, Automation, Presets).
3. Performance: < 5% CPU (Idle), < 20% (Full Modulation), Spektrogramm ≥30fps.

**Decisions**
- FFT-Library: JUCE FFT (MVP) → ggf. FFTW3 später
- Stereo-Strategie: Pro-Channel Verarbeitung
- Auto-Detect Sensitivität: Moderate Thresholds, User-anpassbar
- Preset-Format: JUCE ValueTree (XML)
- Modulations-Limit: Max 32–64 Connections
- Phase-Vocoder: Immer aktiv (Konsistenz)
- Sidechain: Realtime-Analyse mit Freeze-Button
- Timeline-Länge: DAW-Tempo-basiert (4–16 Bars)
- Macros: 8 Global
- UI-Minimum: 1000×700 px, einklappbare Sidebar

**Further Considerations**
1. Portability: Windows (x64), macOS (Intel + ARM), Linux (optional)
2. DAW-Support: VST3, AU (macOS), AAX (optional später)
3. Documentation: Video-Tutorials, Preset-Library mit Anwendungsbeispielen
4. Erweiterungen (Post-MVP): Convolution-Reverb, MIDI-Learn für alle Parameter, OSC-Netzwerk-Control, Export-Funktionen

---

Open questions to confirm with the user (summary):
- FFT library choice (default: JUCE FFT)
- Stereo processing strategy (default: per-channel)
- Sidechain real-time vs snapshot (default: real-time with freeze)
- Preset format (default: ValueTree XML)
