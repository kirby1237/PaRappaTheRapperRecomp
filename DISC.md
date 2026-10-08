# Playing the Rhythm Timing Assist build

This build is based on kirby1237's PaRappaTheRapperRecomp. Supply your own
PaRappa the Rapper USA disc image (SCUS-94183). No disc image, retail BIOS,
memory card or local calibration settings are included.

Supported Track 01 image: 727,718,208 bytes; MD5
`007a1016d499e5fe6eb2702f41ca6c63`. Keep the CUE and its referenced BIN together.

On Windows, extract the ZIP and run `PaRappaTheRapper_Recompiled.exe`. On
Linux, extract the ZIP and run `./PaRappaTheRapper_Recompiled`. Use recomp-ui
to select your disc; the bundled OpenBIOS is available for booting.

Open **Mods / Accessibility / Rhythm Timing Assist** to enable the mod and
adjust its controls. It is disabled by default. Extra early and late tolerance
default to 10 ms each when enabled; latency compensation defaults to zero.
Positive compensation judges a press earlier. Save your selection in the UI.
The mod applies after input conversion, so controller and keyboard presses
use the same judgement settings. Configure your controller in the UI.
PaRappa is locked to a digital PSX pad because it does not recognize Analog
mode. Existing saved Analog selections are overridden. The supplied default
mapping uses the D-pad for directions and leaves both sticks unbound; face and
shoulder buttons keep their usual bindings. A modern DualShock/DualSense or
Xbox controller can still be used as a digital pad. If upgrading an existing
installation, clear any saved stick-direction bindings in the controller UI
to match the new D-pad-only defaults.

Stage 1 was investigated and exercised with stock, forgiving, one-sided and
signed-offset timing probes. Its stock half-window is about 45 ms; the default
assist widens it to about 57 ms. Wider settings are capped at the midpoint
between timing cells. Other stages and full-stage completion are not yet
systematically validated. This is an experimental preview.

The framework now honors `[audio] buffer_ms` in `game.toml`. Without that
section it retains the upstream 180 ms target. For a lower target, add:

```toml
[audio]
buffer_ms = 60
```

The local Stage 1 probe reported zero underruns with that target; queue fill
and actual output delay vary. If you hear dropouts, increase the target.
Calibrate the judgement offset in 5 ms steps for your controller and audio
setup. Changing output devices can change the offset you need.
