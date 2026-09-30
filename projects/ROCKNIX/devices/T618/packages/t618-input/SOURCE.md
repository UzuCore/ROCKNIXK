# RG405M analog adapter provenance

- Mainline polling/IIO interfaces: `drivers/input/joystick/adc-joystick.c` in
  `beebono/linux-mainline-sprd@464b3e7bf45bb300e190339977abba36c8078e1f`.
- GPIO mux reference: `ROCKNIX/rocknix-joypad@d02ed13aae08113f6f9e0e9d699cb29bb3450fa2`.
- RG405M values: GammaOS v1.5.1 `singleadcjoy.ko` and DTBO entry 1.
  The same vendor_boot and dtbo bytes are present in GammaOS Next v1.1.
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
