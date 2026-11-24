# io_extension

Driver for the CH32V003 I/O expander (I2C 0x24: reset, backlight PWM, battery ADC)
of the Waveshare ESP32-C5-LCD-2.73. Copied from the Waveshare
demo package for this board (`ESP-IDF-V553/*/components/io_extension`,
distributed together with the Apache-2.0 licensed Waveshare BSP, see
`LICENSE-Waveshare-Apache-2.0`). The original file headers are kept.

Local changes: finite I2C timeouts (100 ms instead of "wait forever"),
rate-limited error prints, and `IO_EXTENSION_Init()` now writes the output
register with IO5 (USB/CAN select, 0 = USB) low and everything else high.
