| Supported Targets | ESP32-C6 |
| ----------------- | -------- |

# _ESP-NOW Logger_

Firmware for ESP32-C6 for logging events while transmitting soil moisture sensor data over different protocols with different options.

## Structure
* `receiver`: ESP-IDF project for the receiving ESP32 
* `sender`: ESP-IDF project for the sending ESP32 with the connected soil moisture sensor
  
## Development
1. Install esp-idf on your PC following the instructions from Espressif (see [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/index.html) for details)
2. Open the `receiver` or `sender` project and build, flash, monitor etc. from there.

## Creating a release
When hosted on GitHub, use the `.github/workflows/build-release.yml` Action to create a new release.
> NOTE: A valid Tag must be set on commit to trigger the Action. (e.g. `v0.2.8`).

# Attributions for used code
Both `receiver` and `sender` project are based of the example project from Espressif available under [https://github.com/espressif/esp-idf/tree/master/examples](https://github.com/espressif/esp-idf/tree/master/examples). A lot of the content in this repository is unchanged from the example.

Multiple functions are not created by me and the respective origin of those has been noted above each function in the code. The content of these functions is only slightly altered to fit in this project.

Furthermore a lot of individual functionality represents examples given in the ESP-IDF Programming Guide available under [https://docs.espressif.com/projects/esp-idf/en/stable/esp32/index.html](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/index.html).

# Context
This repository was created during the creation of a master thesis at Technische Universität Dortmund - Fakultät für Informatik - Lehrstuhl für eingebettete Systeme, AG Systemsoftware (LS-12).

More details in german can be found under the title of the thesis: "Evaluation drahtloser Kommunikationsverfahren von eingebetteten Sensorknoten am Beispiel eines Bodenfeuchtesensors" (not yet available).

If you want to use the presented code in your own thesis or want to expand it and have any questions about it, feel free to [contact me on 	
&#120143; (formerly Twitter) @philteb](https://twitter.com/philteb) or any other way you can find my contact information.

&copy; 2024 Philipp Sebastian Tebbe (except stated attributions seen above)