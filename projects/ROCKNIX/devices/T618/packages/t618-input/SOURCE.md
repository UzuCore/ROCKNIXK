# RG405M/RG405V analog adapter provenance

- Mainline polling/IIO interfaces: `drivers/input/joystick/adc-joystick.c` in
  `beebono/linux-mainline-sprd@464b3e7bf45bb300e190339977abba36c8078e1f`.
- GPIO mux reference: `ROCKNIX/rocknix-joypad@d02ed13aae08113f6f9e0e9d699cb29bb3450fa2`.
- RG405M values: GammaOS v1.5.1 `singleadcjoy.ko` and DTBO entry 1.
  The same vendor_boot and dtbo bytes are present in GammaOS Next v1.1. The RG405V
  device tree exposes the same ADC, mux and four power GPIO assignments.
  The loaded RG405V vendor module does not request its legacy GPIO16 property:
  its live GNU build-id (18650777178b050a) matches the retained vendor_boot
  module, SHA256 7b595227c3b7ec19061bf03dc1b30c716547367e79f023e081eee87079441b57.
  The different file at /vendor/lib/modules/singleadcjoy.ko is not the loaded
  module; its build-id is a6cfc0efad2ecc6d. Requesting GPIO16 in the Linux V DT
  drove the centered ADC to 51 mV. Forcing HIGH restored a centered reading
  but caused cross-axis interference; that experiment is not a supported fix.
  The V DT therefore omits the unused GPIO16 property. The adapter reports
  the V-specific input device name from its OF match.
- Vendor `joypad_amux_select` at 0x4b84: enable physical LOW, A states
  0/1/0/1, B states 0/0/1/1, 10--20 us settling, axes RY/RX/Y/X.
- Vendor `joypad_adc_check` at 0x4c30: processed mV, normalization divisor
  1850, software scale 2, axial deadzone 216, positive/negative gain 200%.
  The multiply-high signed division was exhaustively compared with the
  integer formula for all integer inputs from 0 to 5000 mV.
- Power GPIOs 19/21/22/24 are physical HIGH in the vendor probe, regardless
  of the legacy DT flags, which the raw GPIO API ignores.

This is a source-prepared external module, not a completed hardware test.
It does not use the Qualcomm SPI/GENI driver with a singleadc name.
It reads the SC2730 processed channel, not an unadvertised RAW channel.
Physical center calibration happens when the input device opens; release
the sticks when opening/calibrating the controller. InputPlumber combines
its four axes with GPIO buttons and SC2730 vibration.

RG405V loaded vendor module `joypad_probe` at 0x44c8--0x44d8
sets IIO_CHAN_INFO_SCALE to 1 (val2=0). The V adapter repeats that call
only for the RG405V OF match; the working RG405M ADC setup is preserved.
The measured V button order is A=BTN_WEST, B=BTN_SOUTH, X=BTN_NORTH,
Y=BTN_EAST (valid second capture; the first capture was discarded).
