# RG405M source adaptations

Base kernel: `beebono/linux-mainline-sprd@464b3e7bf45bb300e190339977abba36c8078e1f`.
Original headset implementation:
`marohinmark/kernel_ums512_5.4@ead7c4e34de9a1465cb9badd7e0cf56691800b16`,
`kernel_modules/kernel5.4/audio_driver/sprd/codec/sprd/sc2730/codec/`.
Existing copyrights and GPL-2.0 notices are retained in the source files.

`source-manifest.json` identifies every original and adapted file. The T618-only
kernel post-patch hook calls `apply_sources.py`, which verifies all inputs before
writing to an explicit `linux-*` package build directory. The separately retained
kernel reference checkout is not modified. No common mkimage changes are used.

## DSP selector

The RG405M GammaOS Next `lib/hw/audio.primary.ums512.so` is ARM32 Thumb, SHA256
`49630f6f2adcfd9d5157c8afb1603fb52a5e25d2b9c71beaf15a9c2add4071ee`.
Function `0x2ca6c` constructs `profile_slot << 24 | parameter_id << 16 | dsp_case`.
Its normal-playback caller passes case 0. The HAL parameter-name table at
`0x5c39c` gives speaker parameter 75 and headphone parameter 70; the matching
vendor `dsp_vbc.xml` gives slots 57 and 56. This produces `0x394b0000` and
`0x38460000`, respectively. The literal at `0x2a650` resolves to
`DSP VBC Profile Select` through PC `0x2a3ee`.

The prior `0x0fffffff` mixer range could not represent either selector. The new
range is `0x7fffffff`, with actual profile bounds checked separately. Firmware
headers and allocation sizes are checked; a failed IPC does not commit the
selector; the ALSA callback returns an error or changed-state flag, not the
selector integer. User buffers are bounded and incomplete payloads are rejected.

## Headset

The original SC2730 interrupt/ADC/type/button state machine replaces the old
no-op headset object. GPIO descriptors, ASoC jack calls, wakeup sources and
resource cleanup match the pinned mainline APIs. Android-only touchscreen
notifications are not imported. The stock RG405M binding is PMIC EIC6,
ADC channel 20, NC jack, 10 ms debounce, 2950 three-pole threshold and the original
button ranges. Calibration cells are the stock PMIC eFuse ranges 0x28/0x2c.

The driver supplies `Headset Jack` and a microphone-masked `Headset Mic Jack`.
UCM consumes both and mutes the speaker/microphone alternatives on insertion.
ADC errors cannot disappear when combined with a positive sample. Probe errors
propagate, and error/unload paths cancel work and release owned resources.

## Charging

RG405M temperature policy gates the final CE register write, not just the polling
callback. Failed/missing/stale temperature, suspend and shutdown request inhibition;
fresh recovery requires the configured hysteresis margin. State updates, register
read/modify/write and readback share the I2C lock. Short I2C transfers and readback
errors fail; raw reset/register writes cannot bypass the guard.

The verified 33 mOhm charge/termination steps are 496/62 mA. On AW32257 the inherited
input-current-limit setter is a no-op: `ti,current-limit = <500>` is NOT proof of
a 500 mA input clamp. Physical inhibit cannot be guaranteed if the bus itself is
unavailable; that case is reported as unknown and must be checked on hardware.
Suspended charger/watchdog behavior requires the post-build hardware test.

## Validation boundary

Five adapted C translation units were checked with actual pinned kernel headers
and the target cross compiler, without producing/linking a kernel or module.
Host tests execute the actual extracted C policy functions with explicit memory,
IPC and lock substitutes. They are not electrical/I2C or on-device tests.
Detailed logs, reverse-engineering listings and tests are under the local project
`08_build/prebuild4`. A linked build, real headset/charging test and physical boot
remain later validation stages; none is implied by source-test success.
