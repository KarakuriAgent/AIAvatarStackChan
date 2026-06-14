# AtomS3R Cam Grove Example

Grove-linked camera child firmware for AtomS3R + AtomS3R-CAM.

In the production `stackchan/basic` AtomS3R firmware, the parent sends Wi-Fi networks and a hashed `camera_token` to this camera child over Grove I2C, polls the camera IP over Grove, and captures JPEG images over HTTP when a `VISION` command is received.

The child receives Wi-Fi settings and a camera-only auth token over Grove I2C, stores them in NVS, connects to Wi-Fi, and serves `/camera`, `/stream`, and `/status`.

Grove/I2C uses the shared AtomS3R custom port pins: SDA=`G2`, SCL=`G1`, address `0x42`. Use a straight Grove cable between the AtomS3R and AtomS3R-CAM custom ports.

## Build

```sh
pio run -d examples/atoms3r_cam_grove
pio run -d examples/stackchan/basic -e atoms3
```

## Upload

Flash each device over USB. Put the AtomS3R-CAM into download mode when flashing it.

```sh
pio run -d examples/atoms3r_cam_grove -t upload --upload-port /dev/ttyACM0
pio run -d examples/stackchan/basic -e atoms3 -t upload --upload-port /dev/ttyACM0
```

## Operation

1. Boot the camera child and AtomS3R parent with Grove connected.
2. The parent sends the current config Wi-Fi settings to the camera over Grove.
3. The camera connects to one of the configured Wi-Fi networks and reports its IP over Grove.
4. After Wi-Fi connects, the child initializes the camera so the first `VISION` capture can start immediately.
5. When the server sends a `VISION` command, the parent calls the camera child `/camera` endpoint and sends the returned JPEG as the vision image.
6. For browser debug, open `http://<camera-ip>/stream` on the same Wi-Fi. Use the current `camera_token` as `?key=<token>` or the `X-Camera-Key` header when debugging protected endpoints.
