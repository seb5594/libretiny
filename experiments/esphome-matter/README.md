# ESPHome / LibreTiny: Vorbereitung für Matter über WLAN

**Stand: Experiment, kein Matter-Server.** Dieses Verzeichnis enthält eine
kompilierbare ESPHome External Components, vier Diagnoseprofile, zwei Beken-Profile
mit nativem Matter-Speicheradapter, einen IPv6-Testclient und Tests. Es implementiert
weder Pairing noch Matter-Cluster oder Zigbee.

Ziel bleibt ein nativer Matter-Port für **BK7231N zuerst, BK7238/Tuya T1 danach**.
LibreTiny bleibt die Buildplattform. LN882H und RTL8720C sind weitere Kandidaten.
Die Ergebnisse und die noch fehlenden Teile sind in
[Machbarkeit](docs/feasibility.md), [Architektur](docs/architecture.md) und
[Prüfergebnisse](docs/validation.md) beschrieben.

## Was bereits funktioniert

- Der neue `libretiny_matter_storage`-Adapter implementiert die originale
  `chip::PersistentStorageDelegate`-Schnittstelle über LibreTinys bestehende FlashDB.
  Schlüssel bleiben vollständig erhalten; Binärwerte, leere Werte, Teil-Lesezugriffe,
  Löschung, volle Partition und Persistenz nach Prozessneustart sind getestet.
  Aufbau, Grenzen und Nutzung: [Matter-Speicheradapter](docs/storage.md).
- ESPHome validiert die Konfiguration und erzeugt Firmware über LibreTiny.
- Auf BK7231N und BK7238/T1 wird IPv6 einkompiliert. Die Komponente kann auf einem
  Testgerät einen nicht blockierenden UDP-Echo-Endpunkt öffnen und der experimentellen
  IPv6-Multicastgruppe `ff02::114` beitreten.
- Logs melden IPv6-Adressen, Heap, kleinsten freien Heap seit Start, größten freien
  Block (soweit vom SDK gemeldet) sowie empfangene/gesendete Testpakete. LibreTinys
  Standardimplementierung der Blockgrößenabfrage liefert null; das wird ausdrücklich
  als `unavailable` ausgegeben und nicht als gemessener Speichermangel interpretiert.
- Derselbe C++-Code wird als ESPHome-Hostprogramm gebaut und über echte IPv6-Sockets
  getestet. Das ersetzt keinen Test der Beken-Funktreiber.
- LN882H/RTL8720CF liefern zunächst eine IPv4-/Speicher-Baseline. Die
  IPv6-Freigabe dieser Familien fehlt in ESPHome 2026.10.0.

Die Komponente verwendet ESPHomes `network`, `socket` und `PollingComponent` sowie
LibreTinys vorhandene Heap-APIs. Der Paketpfad verarbeitet höchstens ein Datagramm
pro 20 ms, nutzt einen festen 21-Byte-Puffer und erzeugt keine eigene dynamische
Paketwarteschlange. Diese Begrenzung macht die gemessene Latenz ausdrücklich
**nicht** zu einem maximalen Durchsatz- oder Matter-Latenzbenchmark.

## Lokal bauen

Python 3.12 und eine C++-Toolchain für die Hosttests installieren. Im Repository:

```sh
cd experiments/esphome-matter
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements-dev.txt
cp examples/secrets.yaml.example examples/secrets.yaml
# In secrets.yaml die WLAN-Daten des Testnetzes eintragen.
esphome compile examples/bk7231n.yaml
esphome compile examples/bk7238-t1.yaml
# Dieselben Boards mit dem nativen CHIP-Speicheradapter:
esphome compile examples/bk7231n-storage.yaml
esphome compile examples/bk7238-t1-storage.yaml
```

Für die weiteren Familien:

```sh
esphome compile examples/ln882h.yaml
esphome compile examples/rtl8720cf.yaml
```

Die Profile pinnen deinen LibreTiny-Fork auf den untersuchten Commit. Sie verwenden
keine veränderten Bootloader- oder Flashpartitionen. Die Messfirmware konfiguriert
keine Relais/GPIOs, keine ESPHome-API, keinen Webserver und kein OTA. Sie ersetzt
beim Flashen die bisherige Geräteanwendung und ist für einen UART-Testaufbau gedacht.
Vor dem Test das vorhandene Image einschließlich Kalibrierungsdaten sichern und
das tatsächliche Modulprofil prüfen. Ein Netzspannungsgerät nur spannungsfrei und
über eine passende, isolierte Niederspannungsversorgung am UART testen.

Die erzeugte `firmware.uf2` mit den üblichen LibreTiny-/ltchiptool-Werkzeugen
flashen; **keine Binärdatei des fremden Matter-SDKs auf ein Tuya-T1-Modul schreiben**.
Die Beispiel-Zugangsdaten sind Platzhalter, auch in CI-Builds.

## IPv6 auf BK7231N oder T1 messen

1. Boot- und WLAN-Logs über UART aufzeichnen. Eine Adresse `fe80::...` genügt für
   einen Test im selben LAN; ein IPv6-Internetzugang ist dafür nicht erforderlich.
2. Auf einem Rechner im selben LAN testen (Interface anpassen):

   ```sh
   python tools/check_udp.py 'fe80::1234%wlan0' --count 20
   python tools/check_udp.py ff02::114 --interface wlan0 --count 5
   ```

3. Nach WLAN-Unterbrechung und Neustart wiederholen. Für spätere Speichervergleiche
   insbesondere `heap_free`, `heap_min` und `heap_max_block` nach dem WLAN-Start und
   nach mindestens 30 Minuten erfassen.

Der Client gibt JSON aus und liefert Exitcode 0 nur, wenn jede Anfrage mindestens
eine passende Antwort erhielt. Beim Multicasttest können mehrere Testgeräte antworten.
Die Interface-Nummer im Client bezeichnet **den Rechner**, nicht die MCU.
`multicast_joined=YES` im Firmwarelog bestätigt nur die lokale Gruppenanmeldung;
erst eine Antwort auf den Multicasttest belegt die Übertragung durch das WLAN.
AP-Isolation und Multicastfilter können diesen Test beeinflussen.

Das Protokoll ist ausschließlich `LTM1` plus 16 zufällige Bytes, zurückgesendet
ohne Verlängerung. Es verwendet UDP-Port 49152, nicht den Matter-Port 5540.
Es prüft keine Matter-Nachrichten, mDNS-Dienste oder kryptografischen Sitzungen.

## In vorhandener ESPHome-Konfiguration verwenden

```yaml
external_components:
  - source:
      type: local
      path: /pfad/zu/libretiny/experiments/esphome-matter/components
    components: [libretiny_matter_probe]

# IPv6 derzeit nur für die Beken-Testprofile aktivieren.
network:
  enable_ipv6: true
  min_ipv6_addr_count: 0

libretiny_matter_probe:
  udp_echo: true
  port: 49152
  interface_index: 0
  update_interval: 30s
```

`udp_echo` ist standardmäßig ausgeschaltet. Mit `false` bleibt nur die Diagnose.
`interface_index: 0` lässt lwIP das Standardinterface für Multicast wählen; bei
Bedarf kann ein bekannter MCU-Interfaceindex angegeben werden. Die Komponente
leitet daraus keine Aussage „Matter-fähig“ ab.

Die Testprofile setzen `min_ipv6_addr_count: 0`, damit ein fehlgeschlagener
IPv6-Adressaufbau beobachtbar bleibt und den allgemeinen Verbindungsstatus nicht
blockiert. Ein späterer Matter-Server muss selbst auf eine brauchbare IPv6-Adresse
warten. Für Vergleiche mit einer bestehenden Geräteanwendung die Komponente dort
ergänzen; die Minimalfirmware misst deren zusätzlichen Speicherverbrauch nicht.

## Entwicklung prüfen

```sh
ruff check components tools tests
autoflake -r --check --remove-unused-variables components tools tests
black --check components tools tests
(cd ../.. && isort --profile black . --check-only)
clang-format --dry-run --Werror components/libretiny_matter_probe/*.h components/libretiny_matter_probe/*.cpp components/libretiny_matter_storage/*.h components/libretiny_matter_storage/*.cpp tests/storage/*.h tests/storage/*.cpp
esphome compile tests/host.yaml
python tools/fetch_flashdb.py
pytest -q
```

Die GitHub-Actions-Datei `matter-probe.yml` führt Hosttests und sechs Firmwarebuilds
aus. [Prüfergebnisse](docs/validation.md) trennen lokale Builds, Hosttests und die
noch ausstehenden Messungen auf echter Hardware.

Dieses Verzeichnis kann später als separates Komponenten-Repository ausgegliedert
werden. Für diesen ersten Stand wird kein weiteres Repository benötigt.
