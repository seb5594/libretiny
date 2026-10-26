# Matter über WLAN für ESPHome und LibreTiny

**Nativer Entwicklungsserver für BK7231N und BK7238/Tuya T1.**
`libretiny_matter_server` verbindet den Project-CHIP-Stack **rs-matter** mit
ESPHome, LibreTiny, FreeRTOS, lwIP und der vorhandenen FlashDB. Ein virtueller
On/Off-Licht-Endpunkt lässt sich mit einem unabhängigen ConnectedHomeIP-Controller
koppeln, schalten, lesen und abonnieren. Manueller Matter-Kopplungscode und
QR-Nutzlast werden auf Wunsch ausgegeben; ein lokales Werkzeug erzeugt PNG/SVG
und eine druckbare Seite. Fabric und Schaltzustand überleben
Neustarts. Die Firmware verwendet die **offizielle LibreTiny-Version 1.14.0**.

**Auf echter Beken-Hardware noch nicht ausgeführt.** Host-Interoperabilität,
MCU-Build und simulierte Plattformtests sind belegt; Funkbetrieb, tatsächlicher
Heap-/Stack-Spitzenbedarf und PASE-Laufzeit auf ARM9 benötigen UART-Messungen.
Das ist ein konkreter Testkandidat, kein zertifiziertes oder fertig vermessenes Produkt.

## Einstieg

- **[Bauen, in bestehende Geräte einbinden und koppeln](docs/matter-server.md)**
- [Architektur und Wiederverwendung](docs/architecture.md)
- [Prüfergebnisse und Grenzen](docs/validation.md)
- [Hardware-Machbarkeit, Zigbee/BLE und Quellen](docs/feasibility.md)

BK7231N hat Priorität. BK7238/T1 wird mit demselben Backend gebaut. LN882H und
RTL8720CF haben Diagnose-/Codec-Profile; der native Server ist für diese Plattformen
noch nicht freigegeben. BLE ersetzt keine IEEE-802.15.4-Hardware: Zigbee und
Matter-over-Thread sind mit den untersuchten Funkmodulen nicht implementierbar.
Matter-over-Wi-Fi benötigt BLE nicht für den laufenden Betrieb; dieser Port richtet
sich über ein **bereits von ESPHome eingerichtetes WLAN** ein.

## Komponenten

| Komponente | Aufgabe |
| --- | --- |
| `libretiny_matter_server` | PASE, CASE, Attestation mit Testzertifikaten, Fabric/ACL, Interaction Model, mDNS, MRP, virtueller On/Off-Endpunkt und Subscriptions über rs-matter |
| `libretiny_matter_storage` | Gemeinsamer FlashDB-Speicher; CHIP-Delegate und synchronisierte Anbindung des Matter-Workers |
| `libretiny_matter` | Unveränderte C++-CHIP-Header/Codecs für Speichervertrag und unabhängige Codec-Tests |
| `libretiny_matter_protocol` | [72 lokale Codec-Selbstprüfungen](docs/protocol-tests.md), optional auf vorhandenen Geräten |
| `libretiny_matter_probe` | IPv6-Unicast-/Multicastdiagnose, Heap-Logs, eigenes `LTM1`-Echo auf UDP/49152 |

Das On/Off-Licht verwendet **keine GPIOs und keine vorhandenen Relais**. Die
Zuordnung zu echten ESPHome-Entities ist noch offen. Das Matter-WLAN-Cluster kann
den bestehenden Netzstatus melden; Scan und Wechsel der WLAN-Zugangsdaten werden
explizit abgewiesen. BLE-Provisionierung, Matter-OTA, Multi-Fabric-Belastungstests,
Produktzertifikate und ein benutzerseitiger Matter-Resetknopf fehlen noch.
ESPHome-API/OTA lassen sich weiterhin über IP nutzen; ESPHomes eigener mDNS-Responder
muss für diesen Stand abgeschaltet sein.

## Schnell bauen

Linux x86_64, Python 3.12, C/C++-Compiler und Rustup installieren. Den Rustup-Installer
von [rust-lang.org](https://www.rust-lang.org/tools/install) verwenden, falls nötig.
Für den ersten Rust-Build mindestens ungefähr 6 GiB verfügbaren RAM einplanen.

```sh
cd experiments/esphome-matter
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements-dev.txt
rustup toolchain install nightly-2026-10-24 --profile minimal --component rust-src,rustfmt
cp examples/secrets.yaml.example examples/secrets.yaml
# WLAN-Zugangsdaten in examples/secrets.yaml eintragen.
esphome compile examples/bk7231n-matter.yaml
esphome compile examples/bk7238-t1-matter.yaml
```

Das PlatformIO-Skript baut die gepinnte Rust-Bibliothek für `thumbv5te-none-eabi`
und verlinkt sie in die normale LibreTiny-Firmware. Kein anderer Bootloader,
keine geänderte Partitionstabelle und keine Binärbibliothek eines fremden Matter-SDKs.
Der erste Build lädt Cargo-Abhängigkeiten und dauert einige Minuten. Bei mehreren
Buildprofilen kann `LTM_CARGO_TARGET_DIR` auf ein gemeinsames absolutes Cache-Verzeichnis
zeigen. Standardmäßig ist Rust auf einen Compilerjob begrenzt.

Für API und OTA zusätzlich gibt es `bk7231n-matter-existing.yaml` und
`bk7238-t1-matter-existing.yaml`. Die Beispielprofile sind generisch und ersetzen
beim Flashen die bisherige Geräteanwendung. Vor einem UART-Test das tatsächliche
Modul/Flashlayout prüfen und ein vollständiges Backup einschließlich RF-Daten anlegen.
Ein Netzspannungsgerät nur spannungsfrei und mit passender isolierter Versorgung testen.

## Tests reproduzieren

```sh
esphome compile tests/host.yaml
esphome compile tests/protocol-host.yaml
python tools/fetch_flashdb.py
pytest -q
```

Die vollständigen Matter-Netzwerktests verwenden einen **unabhängigen C++-Controller**,
die identische Rust-Serverbibliothek, den identischen UDP-Adapter und echte FlashDB
auf einem NOR-Simulator. Einrichtung und Aufruf stehen in
[der Serveranleitung](docs/matter-server.md#unabhängige-interoperabilitätstests).
GitHub Actions führt die Hosttests, diese Interoperabilitätstests und 14 Firmwareprofile
aus. Controller-Zugangsdaten und simulierte Flashabbilder werden nicht veröffentlicht.

Die getrennten Diagnoseprofile `bk7231n.yaml`, `bk7238-t1.yaml`, `ln882h.yaml` und
`rtl8720cf.yaml` bleiben verfügbar. Bei Beken kann `tools/check_udp.py` das
Diagnose-Echo über IPv6 testen; dieses Echo ist unabhängig vom Matter-Protokoll.
