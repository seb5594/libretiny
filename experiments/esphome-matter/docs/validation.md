# Prüfstand vom 26.10.2026

## Nativer Matter-Server

ESPHome 2026.10.0, offizielles LibreTiny 1.14.0, Rust nightly-2026-10-24,
rs-matter `7195e2b1f45df94ee1bc63605b7db91a6878b9dc`.
Unabhängiger Controller: ConnectedHomeIP-C++-SDK aus
`home-assistant-chip-core` und `home-assistant-chip-clusters`, beide 2025.7.0.

Der automatisierte Interoperabilitätslauf besteht **60 Prüfungen in sieben Phasen**:

| Phase | Nachweis |
| --- | --- |
| Erstkopplung | QR-Kopplung per mDNS, unabhängiger Codevergleich und Prüfzifferprüfung, falschen PIN abweisen, PASE, PAA/DAC/PAI-Attestation, CSR, AddNOC, CASE, CommissioningComplete |
| Betrieb | Off/On/Toggle und Read, Subscription, BasicInformation, NodeLabel schreiben/lesen/persistieren, Schreibschutz von OnOff, ACL-Ablehnung, ungültiger Endpunkt, 98 fehlerhafte UDP-Datagramme und anschließender gültiger Read |
| Prozessneustart | Fabric aus tatsächlicher FlashDB, CASE und persistiertes OnOff=true; Betriebstests erneut |
| Warmstart | Rust-Future/Stack stoppen, Sockets schließen, erneut initialisieren; gleiche Fabric und Zustand; Betriebstests erneut |
| Speicherfehler + Wiederanlauf | NOR verweigert Fabric-Schreibvorgang; CommissioningComplete scheitert; nach Neustart kein gültig gespeicherter Fabric, erneute Kopplung per manuellem Code mit anderem PIN/Discriminator und Betrieb erfolgreich |
| Setup-Ausgabe | Codes bei deaktivierter Ausgabe sowie nach Kopplung/Neustart unterdrückt |
| Entropiefehler | Entropiequelle liefert Fehler; Stack stoppt vor Einrichtung, kein unsicherer Fallback |

Ergebnisse: [maschinenlesbare Zusammenfassung](server-test-results.json).
Der Server verwendet dieselbe Rust-Bibliothek, denselben UDP-Adapter und denselben
FlashDB-Delegate wie die MCU-Firmware. Der Host ersetzt FreeRTOS/lwIP/Treiber durch
POSIX; FlashDB selbst läuft über den gemeinsamen 32-KiB-NOR-Simulator. Das simuliert
kein physisches Abschalten während einer Flashoperation.

Zusätzlich bestehen **90 pytest-Tests**. Dazu gehören:

- Der tatsächliche ESPHome-/FreeRTOS-Brückencode mit Plattform-Doubles und ASan/UBSan:
  Start erst mit WLAN und IPv6, Wiederverbindung, IPv6-Verlust, zu wenig Heap,
  Task-Erzeugungsfehler, vollständige TRNG-Pufferbefüllung, Entropiefehler und
  festhängende TRNG-Ausgabe sowie ausgerichtete Allokation/Überlaufgrenzen.
- Echte ESPHome-Konfigurationsvalidierung einschließlich verbotener Passcodes,
  Plattform-, IPv6-, mDNS-, Discriminator- und Zeitfenstergrenzen.
- QR-Bilder mit unabhängigem ZXing-Decoder zurückgelesen, weiße Ruhezone,
  zusammengehörige Codepaare aus Logs und Ablehnung mehrdeutiger/ungültiger Eingaben.
- SHA-256-Prüfung unveränderter Upstream-Dateien und des öffentlichen Test-PAA.
- Bisherige IPv6-Probe-, FlashDB- und CHIP-Codec-Tests einschließlich 20.000
  reproduzierbarer Parser-Zufallseingaben mit Address-/UndefinedBehaviorSanitizer.
  Die separate ESPHome-Codec-Hostkomponente besteht 72 Einzelprüfungen.

Black, isort (Repository-Konfiguration), autoflake, Ruff, clang-format und Rustfmt
prüfen die geänderten Quellen. GitHub Actions enthält die Hosttests, den unabhängigen
Controllerlauf und 14 Firmwareprofile. Die Ergebnisse eines konkreten Commits sind
am zugehörigen Actions-Lauf abzulesen; lokale Ergebnisse ersetzen diesen Lauf nicht.

## Firmwarebudget

Die Serverprofile bauen für BK7231N und BK7238/Tuya T1 in der regulären
LibreTiny-Apppartition mit **1.083.136 Byte Flashlimit**. Die genauen finalen
Buildwerte stehen in der nachfolgenden Tabelle; `*-existing` ergänzt API und OTA.

| Profil | Flash | statisches RAM |
| --- | ---: | ---: |
| BK7231N | 871.066 B | 106.884 B |
| BK7238/T1 | 971.968 B | 110.784 B |
| BK7231N + API/OTA | 898.446 B | 107.100 B |
| BK7238/T1 + API/OTA | 999.360 B | 111.000 B |

Das statische RAM laut Linker ist **kein gemessener freier Laufzeit-Heap**.
BK7231N nutzt unterschiedliche RAM-/TCM-Bereiche; die Rechnung 256 KiB minus
Linker-RAM ist nicht der verfügbare Heap. Ein 24-KiB-Workerstack wird dynamisch
angelegt. Start mit weniger als 40 KiB freiem Heap wird abgewiesen. Das ist eine
Anlaufgrenze, kein Beweis ausreichender Reserven für jeden Betriebszustand.
SDK-Warnungen bleiben möglich; hier wird keine Warnungsfreiheit des gesamten
Herstellerframeworks behauptet. API-/OTA-Builds prüfen Platz/Linken, keinen realen
OTA-Transfer oder Wiederanlauf nach einem unterbrochenen Update.

## Frühere Diagnose-/Codec-Profile

Diese Werte stammen aus den vorherigen lokal und in CI erfolgreichen Builds.
Sie enthalten **keinen laufenden Matter-Server** und bleiben getrennt vergleichbar.

| Profil | Flash | statisches RAM |
| --- | ---: | ---: |
| BK7231N Diagnose | 500.794 B | 47.620 B |
| BK7238/T1 Diagnose | 601.344 B | 51.528 B |
| LN882H Diagnose | 409.816 B | 94.332 B |
| RTL8720CF Diagnose | 435.769 B | 12.841 B |
| BK7231N Speicher | 502.490 B | 47.644 B |
| BK7238/T1 Speicher | 603.032 B | 51.552 B |
| BK7231N Codec | 508.878 B | 47.684 B |
| BK7238/T1 Codec | 609.416 B | 51.592 B |
| LN882H Codec | 417.792 B | 94.400 B |
| RTL8720CF Codec | 443.117 B | 12.841 B |

LN882H verwendet ein Flashlimit von 1.228.800 B, RTL8720CF im untersuchten
2M/992k-Profil 1.015.808 B. Plattformen zählen RAM-Segmente unterschiedlich;
insbesondere die kleinen RTL-Zahlen sind kein Gesamtverbrauch.

## Plattformabhängigkeiten und Grenzen

| Ziel | SDK | lwIP |
| --- | --- | --- |
| BK7231N | BDK 3.0.33, `841343e64d7584a3a650222ab0b6ea967755352a` | 2.2.0-bdk, `0d1cedf` |
| BK7238 | BDK 3.0.78, `44800e7451ea30fbcbd3bb6e905315de59349fee` | 2.2.0-bdk, `0d1cedf` |
| LN882H | `8794085af8f548b4a1f783fdbab36d86aeff9a74` | 2.1.3-ln882h, `7ef5aac` |
| RTL8720CF | AmebaZ2 `5f7b9378a7f226a46e3b13349844a3f5f07565aa` | 2.1.3-ambz2, `929d2dd` |

LibreTiny löst Frameworkpakete selbst auf; insbesondere Branch-Abhängigkeiten sind
kein vollständiger Paketlock. Cargo-Abhängigkeiten sind dagegen durch Commit und
`Cargo.lock` gepinnt.

**Noch nicht nachgewiesen:** Ausführung auf physischer Beken-Hardware, realer
AP-Multicast, TRNG-Qualität, PASE/CASE-Zeit auf ARM9, Stack-/Heap-Spitzen, physische
Stromausfallsicherheit, FlashDB-Metadatenfehlerpfade, Multi-Fabric-/Gruppenlast,
Langzeitbetrieb, BLE/WLAN-Koexistenz oder Freigabe durch kommerzielle Ökosysteme.
Die Softwaretests verwenden öffentliche Entwicklungszertifikate, keine zertifizierte
Produktidentität. Logs und Hardware-Testfolge: [matter-server.md](matter-server.md).
