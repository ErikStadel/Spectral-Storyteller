---
name: testing-spectral-storyteller
description: Test the Spectral Storyteller JUCE audio plugin UI and functionality. Use when verifying UI changes, layout, or interactive controls in the plugin.
---

# Testing Spectral Storyteller Plugin

## Build Dependencies (Linux)

The plugin requires these system packages to build on Linux:

```bash
sudo apt-get install -y cmake g++ pkg-config \
  libasound2-dev libx11-dev libxrandr-dev libxcursor-dev libxinerama-dev \
  libfreetype-dev libgl-dev libgtk-3-dev libwebkit2gtk-4.0-dev \
  libcurl4-openssl-dev
```

## Building

```bash
cd /home/ubuntu/repos/Spectral-Storyteller
rm -rf build && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The standalone binary is at: `build/SpectralStoryteller_artefacts/Release/Standalone/SpectralStoryteller`

### Known Build Issues

- **PowerShell post-build step**: The CMakeLists.txt has a post-build command that copies the VST3 to a Windows path using PowerShell. This must be wrapped in `if(WIN32)` to avoid breaking the Linux build. If you see `/bin/sh: Syntax error: word unexpected`, this is the cause.
- **libcurl linkage**: JUCE's cmake doesn't always propagate libcurl to the final link targets on Linux. If you see `undefined reference to curl_easy_init`, add `pkg_check_modules(CURL libcurl)` and link `${CURL_LIBRARIES}` in the Linux section of CMakeLists.txt.
- **CMake cache**: If newly installed system packages aren't detected, delete the entire `build/` directory and reconfigure from scratch. Don't just re-run cmake over an existing cache.

## Running the Standalone

```bash
DISPLAY=:0 /path/to/build/SpectralStoryteller_artefacts/Release/Standalone/SpectralStoryteller &
```

- The standalone runs without real audio hardware. ALSA warnings (`open /dev/snd/seq failed`) are non-fatal.
- A notice "Audio input is muted to avoid feedback loop" appears at the top — this is normal.
- Use `wmctrl` to focus and maximize the window for screenshots/recording.

## UI Structure

The plugin UI has these main sections:
1. **Header bar**: "SPEKTRAL // STORYTELLER" title, preset selector, D/W and Gate knobs, version info
2. **Left sidebar**: "OBJEKT-DATENBANK" with Auto-Detect button, "+" button for adding objects, card-style object entries
3. **Input meter strip**: Left edge of spectral view, labeled "In"
4. **Spectral view**: Center area showing frequency spectrum (20Hz-20kHz)
5. **Output meter strip**: Right edge, labeled "Out"
6. **Timeline**: Between spectral view and footer, shows keyframe automation
7. **Modulation Hub**: Bottom-left, LFO/XY selector, XY pad or LFO scope, destination dropdowns
8. **FX Rack**: Bottom-right, horizontal scrollable rack with module cards and knobs

## Testing Tips

- **Creating test objects**: Click the "+" button in the sidebar to open the transform menu (Wavetable: Sine/Saw/Square/Triangle, Datei laden..., Transient). This creates objects for testing.
- **FX Rack requires selection**: The FX rack only shows modules when an object is selected. The message "Select an object in the sidebar to edit FX automation" appears when nothing is selected.
- **ADD FX button**: Click to show popup with available effects (Delay, Filter). Only works when an object is selected.
- **Color-coded objects**: Transient objects get a red dot, tonal/wavetable objects get a blue dot, noise objects get a green dot.
- **Window management**: Use `wmctrl -i -a <window_id>` to focus the standalone window. Use `wmctrl -l` to find window IDs.

## Devin Secrets Needed

None required for testing. The plugin runs entirely locally without external services.
