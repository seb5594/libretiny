# Machbarkeit und Quellen

Untersucht am 24./25.10.2026. Eine bestandene Kompilierung ist kein Nachweis für
Funkbetrieb, Pairing, Interoperabilität oder Zertifizierung.

## Entscheidung pro MCU

| MCU | Zigbee ohne zusätzliche Funkhardware | Matter über WLAN | Nächste technische Hürde |
| --- | --- | --- | --- |
| BK7231N | Kein dokumentiertes IEEE-802.15.4-Funkmodul | Nativer rs-matter-Server gebaut; Host-Interoperabilität bestanden | Funkbetrieb, Laufzeit-Heap/Stack und TRNG auf Hardware messen |
| BK7238 / Tuya T1 | Kein dokumentiertes IEEE-802.15.4-Funkmodul | Derselbe native rs-matter-Server gebaut; offizielles LibreTiny | Hardwaremessung; BLE/WLAN-Koexistenz separat |
| LN882H | Kein dokumentiertes IEEE-802.15.4-Funkmodul | WLAN-Hardware grundsätzlich passend; Port offen | IPv6 durch ESPHome/LibreTiny durchgängig nutzbar machen |
| RTL8720C | Kein dokumentiertes IEEE-802.15.4-Funkmodul | WLAN-Hardware grundsätzlich passend; Port offen | IPv6, knapper RAM und genaue CF/CM-/Flashvariante |

„LN88H“ wird hier als LN882H interpretiert. Für RTL8720C wurde konkret
`generic-rtl8720cf-2mb-992k` gebaut; daraus folgt keine Prüfung aller RTL8720C-Module.

Zigbee benötigt IEEE 802.15.4 PHY/MAC. WLAN und BLE nutzen zwar ebenfalls das
2,4-GHz-Band, bieten aber andere Modulation und Paketformate. In den untersuchten
Chips/SDKs ist keine nutzbare 802.15.4-Unterstützung dokumentiert. Ein Softwareport
der ESPHome-Zigbee-Komponente kann das nicht ergänzen. Ein externer 802.15.4-NCP
wäre ein anderes Hardwareprojekt und benötigte einen neuen Backend-Treiber.
Dasselbe Funkproblem betrifft Matter-over-Thread. Matter-over-Wi-Fi verwendet
dagegen IPv6 über WLAN; BLE kann zur Einrichtung dienen und ist kein Voraussetzung
für den späteren IP-Betrieb. Die erste Portierung soll im bereits eingerichteten
WLAN provisioniert werden; Kompatibilität dieses Ablaufs muss pro Controller
praktisch geprüft werden.

## Beken ist nicht eine einheitliche Plattform

BK7231N und BK7238 verwenden in LibreTiny einen ARM968E-S / ARMv5TE-Build.
„Beken wird von Matter unterstützt“ ist alleine nicht aussagekräftig:

- Der offizielle [Beken-Beispielbuild von connectedhomeip][chip-example] beschreibt
  BK7235 mit Armino und einer RISC-V-Toolchain. Das ist kein Drop-in für BK7231N/T1.
- Das [Sparkleiot-BK7238-Plug-SDK][plug-sdk] enthält dagegen `src/platform/Beken`,
  ARM968E-S-Buildflags, FreeRTOS und ein On/Off-Steckdosenbeispiel. Das ist eine
  relevante Portierungsreferenz. `build.sh` nennt zusätzlich BK7231N-Module;
  diese Einträge allein beweisen keinen funktionierenden BK7231N-Build.
- Dieses SDK bindet vorgebaute Bibliotheken und Quectel-Anpassungen ein
  (`libqladapt.a`, Git LFS). Apache-Lizenzhinweise am CHIP-Code gelten nicht
  automatisch für jede andere Datei oder Binärbibliothek. Es wird in diesem
  Experiment kein Fremd-SDK-Code kopiert oder ausgeliefert.
- Seine [2-MB-Partitionstabelle][sdk-flash] enthält andere App-, OTA-, RF- und
  Matter-Datenbereiche als [Tuya T1 in LibreTiny][t1-flash]. Portierung bedeutet
  daher Schnittstellen anpassen, nicht dessen komplettes Image übernehmen.

## Speicher und SDK-Details

[LibreTinys Beken-Basisprofil][bk-board] nennt 2 MiB Flash und 256 KiB RAM. Der
ausgewiesene logische Anwendungsbereich beträgt 1.083.136 Byte, nicht 2 MiB.
CRC-Blöcke, Bootloader, OTA-Download und Kalibrierungs-/Datenbereiche sind getrennt
zu berücksichtigen. Das T1-Profil hat seine eigene physische Aufteilung.

Beim BK7231N endet die dynamische Heapregion laut [Implementierung][bk-heap] nach
192 KiB im normalen RAM; zusätzlich existieren getrennte TCM-Bereiche. Die Rechnung
„256 KiB minus vom Linker gemeldete RAM-Belegung = freier Heap“ wäre falsch.
WLAN, lwIP und RTOS belegen außerdem zur Laufzeit Speicher. Ein sicherer
Maximalumfang an Matter-Endpunkten lässt sich aus diesen Zahlen nicht ableiten.

Für das Experiment wählt ESPHome BDK 3.0.33 für BK7231N und 3.0.78 für BK7238.
Diese Unterschiede müssen im späteren gemeinsamen Backend gekapselt werden.
Die aktuelle ESPHome-[BLE-Komponente][ble] unterstützt BK7231N, sperrt BK7238
jedoch wegen des SDK-Hängers bei WLAN-STA-Aktivierung mit BLE. Physisch vorhandenes
BLE ist deshalb noch keine fertige Matter-BLE-Provisionierung. Die Minimalprofile
lassen ESPHome unbenutztes BLE abschalten.

Die Entropieanbindung braucht wegen folgender SDK-Eigenschaften besondere Sorgfalt:

- [LibreTinys `lt_rand_bytes()`][random] verwendet `rand()`.
- BDK 3.0.33 verwendet im [mbedTLS-Hardwareadapter][old-entropy] ebenfalls `rand()`.
- Sein [TRNG-Treiber][old-trng] existiert, befüllt bei `BkRandomNumberRead()` aber
  höchstens ein Wort pro Aufruf. Ein großer Bufferaufruf wäre unvollständig.
- BDK 3.0.78 hat einen anderen Adapter und einen überarbeiteten Treiber. Auch
  dort ersetzt ein passender Funktionsname keine Prüfung von Initialisierung,
  Fehlerfällen und Entropiequalität.

Der native Server verwendet deshalb direkt den TRNG-Geräteaufruf, wortweise und
mit Fehler-/Wiederholungskontrolle. Hosttests prüfen volle Puffer, Grenzen und
Fehlerfälle. Er verwendet keinen rand()-Fallback. Die tatsächliche Entropiequalität
und Initialisierung auf Beken-Hardware sind weiterhin zu prüfen. Protokoll- und
Kryptoverfahren stammen aus rs-matter/RustCrypto.

## Wiederverwendung von ESPHome

[DavidvtWout/esphome-matter][esphome-matter] ist ein wertvolles Vorbild für
Endpunktzuordnung und YAML. Die Implementierung basiert aber auf `esp-matter`
und ESP-IDF. Ein zusätzlich erlaubter Plattformname in der Validierung würde
keinen LibreTiny-Port ergeben. BLE-Pairing ist dort laut untersuchtem README
ebenfalls nicht der funktionierende Standardpfad.

Die native [ESPHome-Zigbee-Komponente][zigbee] unterstützt inzwischen ESP32 und
Zephyr und trennt deren Backends. Dieses Muster passt zu einer späteren
plattformneutralen Matter-Komponente: gemeinsames Schema, gemeinsame
Entity-Zuordnung, getrennte Plattformanbindung. Zigbee- und Matter-Cluster sind
dennoch keine austauschbare Binärschnittstelle.

Die vorhandene [ESPHome-Netzwerkkonfiguration][network] unterstützt IPv6 für
Beken mit LibreTiny ab 1.7.0; sie setzt unter anderem `CONFIG_IPV6`. Für LN882H
und RTL87xx fehlt die entsprechende Freigabe. Diese Beschränkung ist eine Aussage
über den untersuchten Softwarestand, keine physikalische Unmöglichkeit von IPv6.

## Einschätzung

Der aussichtsreichste Weg für unveränderte Geräte ist ein schlanker, nativer
Matter-over-Wi-Fi-Port mit zunächst einem On/Off-Endpunkt und Provisionierung
über das vorhandene LAN. BK7231N bleibt Hauptziel; BK7238/T1 dient parallel als
zweites Buildziel. Der Server passt in beide regulären Apppartitionen und koppelt
sich auf dem Host mit einem unabhängigen CHIP-Controller. Ein verbindliches
„läuft auf dem Gerät“ benötigt weiterhin Messungen während PASE/CASE auf ARM9.

Eine externe lokale Matter-Bridge könnte ESPHome-Geräte ebenfalls in mehrere
Ökosysteme einbinden, würde aber zusätzliche laufende Infrastruktur verlangen.
Sie wurde hier nicht als Ersatz für den gewünschten nativen Port implementiert.

[bk-board]: https://github.com/seb5594/libretiny/blob/c81bc9cef83ffd42fcffce32b8b08793b2400b9f/boards/_base/beken-72xx.json
[bk-heap]: https://github.com/seb5594/libretiny/blob/c81bc9cef83ffd42fcffce32b8b08793b2400b9f/cores/beken-72xx/base/api/lt_mem.c
[t1-flash]: https://github.com/seb5594/libretiny/blob/c81bc9cef83ffd42fcffce32b8b08793b2400b9f/boards/_base/beken-7238-tuya.json
[random]: https://github.com/seb5594/libretiny/blob/c81bc9cef83ffd42fcffce32b8b08793b2400b9f/cores/common/base/api/lt_utils.c
[old-entropy]: https://github.com/libretiny-eu/framework-beken-bdk/blob/841343e64d7584a3a650222ab0b6ea967755352a/beken378/func/mbedtls/mbedtls-port/src/tls_hardware.c
[old-trng]: https://github.com/libretiny-eu/framework-beken-bdk/blob/841343e64d7584a3a650222ab0b6ea967755352a/beken378/func/user_driver/BkDriverRng.c
[ble]: https://github.com/esphome/esphome/blob/2026.10.0/esphome/components/bk72xx_ble/__init__.py
[network]: https://github.com/esphome/esphome/blob/2026.10.0/esphome/components/network/__init__.py
[zigbee]: https://github.com/esphome/esphome/blob/c2911a51bd60f45b6cef72c27f8d878e4981b2d8/esphome/components/zigbee/__init__.py
[esphome-matter]: https://github.com/DavidvtWout/esphome-matter
[chip-example]: https://github.com/project-chip/connectedhomeip/blob/4b4a3e405882afaf8161c0e4af244e65fa8f3f94/examples/lighting-app/beken/README.md
[plug-sdk]: https://github.com/Sparkleiot/matter_bk7238_plug_sdk/tree/c10b9a828e4e85d34c0b49705451cf5d9b054752
[sdk-flash]: https://github.com/Sparkleiot/matter_bk7238_plug_sdk/blob/c10b9a828e4e85d34c0b49705451cf5d9b054752/beken378/func/user_driver/BkDriverFlash.c
