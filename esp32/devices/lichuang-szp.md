# Lichuang SZP ESP32-S3 local port

Hardware: ESP32-S3 N16R8, 16 MB flash, 8 MB octal PSRAM.
Source: https://wiki.lckfb.com/zh-hans/szpi-esp32s3/

| Interface | Wiring |
|---|---|
| I2C | SDA 1, SCL 2 |
| ST7789 | SPI3 MOSI 40, CLK 41, DC 39, active-low backlight 42; CS through PCA9557 bit 0 |
| FT6336 | I2C 0x38, polling, no dedicated reset/interrupt GPIO |
| ES8311 | I2C 0x18, I2S output 45 |
| ES7210 | I2C 0x41, I2S input 12; MIC1/MIC2 |
| Shared audio clocks | MCLK 38, BCLK 14, WS 13 |
| PCA9557 | I2C 0x19; bit 1 amp enable, bit 2 camera power-down |
| Button | BOOT GPIO0, active low |

Validation on 2026-10-10: ESP-IDF v6.0.1 builds passed and all 183 host
tests passed without skips. USB flashing passed the device hash checks.
The user confirmed visible Muse output, correct touch and working audio.
Audio self-test measured 16.13 kHz capture / 16.16 kHz playback against 16 kHz.
Speaker/microphone loopback passed at volume 25: both channels captured the
1-6 kHz test tones. Volume 70 and mic gain 30 dB were restored.
BLE pairing completed, credentials persisted, and Wi-Fi reconnected after
reboot. Cloud API connection still times out on the user's direct router
path; the Mac reaches it through Shadowrocket. End-to-end chat is unverified.

Panel initialization must prime SPI mode 2 while PCA9557 LCD_CS remains high,
as the vendor's reset-before-CS sequence does. Selecting it before IDF's
first SPI transaction left the physical screen blank despite a correct LVGL
snapshot. After selecting it, reset and wait at least 120 ms before SLPOUT.
Backlight uses inverted 5 kHz PWM on GPIO42. Touch mirrors raw X before swap.
Two 8-line internal DMA buffers leave room for the 8 KB provisioning stack:
the previous 32-line buffers left only a 6 KB contiguous block after scan,
causing error_operation_in_progress and a BLE disconnect before Wi-Fi join.
Temporary color tests, GPIO dumps and LVGL snapshots are removed in release.

Original 16 MB flash has been backed up outside the source checkout.
The original firmware uses a different partition layout. For the first
installation only, initialize the new NVS area at 0x11000 (size 0xC000)
after backing up the original image. Future Muse reflashes retain NVS.

This local profile disables OTA and eFuse-based NVS encryption. It does not
enable Secure Boot or flash encryption. Use a local board-specific build for
updates. The user SDK token belongs in the ignored build configuration only.

The touch UI defaults to Simplified Chinese. Settings → 中文 / English toggles
immediately and persists across reboots. All settings pages use compact rows
and scrolling at 320×240. Text-entry pages keep the title and input visible
above the four-row QWERTY keyboard; password visibility is switchable.
This is UI localization, not a Chinese input method. SSIDs, passwords and
chat replies retain their original text. Both languages were checked with
on-device snapshots, and persistence was checked with a hardware reset.
The shared UI also builds for FNK0104B with the default English configuration.
