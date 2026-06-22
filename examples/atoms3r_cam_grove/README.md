# AtomS3R Cam I2C Example

I2C-linked camera child firmware for AtomS3R + AtomS3R-CAM.

In the production `stackchan/basic` AtomS3R firmware, the parent sends Wi-Fi networks and a hashed `camera_token` to this camera child over I2C, polls the camera IP over I2C, and captures JPEG images over HTTP when a `VISION` command is received.

The child receives Wi-Fi settings and a camera-only auth token over I2C, stores them in NVS, connects to Wi-Fi, and serves `/camera`, `/stream`, and `/status`.

The AtomS3R parent build in `examples/stackchan/basic` is configured for the Atomic Echo Base back pins: SDA=`G38`, SCL=`G39`. Wire those to the AtomS3R-CAM back pins `G38` and `G39`, with shared `GND`. Connect `5V` only when one side is powering the other.

The camera child build is configured for the AtomS3R-CAM back pins: SDA=`G38`, SCL=`G39`, address `0x42`. To use a different child-side connector, override `ATOMS3R_CAM_CHILD_I2C_SDA` and `ATOMS3R_CAM_CHILD_I2C_SCL` in `examples/atoms3r_cam_grove/platformio.ini`.

For a direct Grove-to-Grove connection, set the parent flags in `examples/stackchan/basic/platformio.ini` to `AIAVATAR_REMOTE_CAMERA_I2C_SDA=2` and `AIAVATAR_REMOTE_CAMERA_I2C_SCL=1`, and the child flags in `examples/atoms3r_cam_grove/platformio.ini` to `ATOMS3R_CAM_CHILD_I2C_SDA=2` and `ATOMS3R_CAM_CHILD_I2C_SCL=1`.

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

1. Boot the camera child and AtomS3R parent with I2C and GND connected.
2. The parent sends the current config Wi-Fi settings to the camera over I2C.
3. The camera connects to one of the configured Wi-Fi networks and reports its IP over I2C.
4. After Wi-Fi connects, the child initializes the camera so the first `VISION` capture can start immediately.
5. When the server sends a `VISION` command, the parent calls the camera child `/camera` endpoint and sends the returned JPEG as the vision image.
6. For browser debug, open `http://<camera-ip>/stream` on the same Wi-Fi. Use the current `camera_token` as `?key=<token>` or the `X-Camera-Key` header when debugging protected endpoints.
