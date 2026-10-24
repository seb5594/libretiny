# Prüfstand vom 24.10.2026

## Lokal erfolgreich

- ESPHome 2026.10.0, Python 3.12.14, LibreTiny 1.14.0 aus
  `seb5594/libretiny@c81bc9cef83ffd42fcffce32b8b08793b2400b9f`.
- Vier Firmwareprofile vollständig kompiliert und verlinkt; UF2-Dateien erzeugt.
- ESPHome-Hostprogramm mit demselben Probe-C++-Code kompiliert und ausgeführt.
- 15 pytest-Prüfungen bestanden: echte IPv6-Roundtrips, fehlender Listener,
  verworfene kurze/falsche/übergroße Datagramme mit anschließendem gültigem
  Roundtrip, IPv6-Scopevalidierung und ungültige ESPHome-Konfigurationen.
- Ruff, autoflake, Black, isort und clang-format erfolgreich. Die Python-Formatierung
  folgt den Black-/isort-Vorgaben des übergeordneten LibreTiny-Repositories.

| Profil | IPv6 / Echo | Flash laut Build | ausgewiesenes Flashlimit | statisches RAM laut Build |
| --- | --- | ---: | ---: | ---: |
| BK7231N Tuya | einkompiliert | 500.794 B | 1.083.136 B | 47.620 B |
| BK7238 Tuya T1 | einkompiliert | 601.344 B | 1.083.136 B | 51.528 B |
| LN882H | aus | 409.816 B | 1.228.800 B | 94.332 B |
| RTL8720CF 2M/992k | aus | 435.769 B | 1.015.808 B | 12.841 B |

Das sind **Diagnosefirmwarewerte ohne Matter**. Das Buildsystem bewertet je nach
Plattform unterschiedliche Speichersegmente; insbesondere die kleinen statischen
RTL-Zahlen dürfen nicht als Gesamtbedarf interpretiert werden. Laufzeit-Heap,
Wi-Fi-/RTOS-Reservierungen, Bootloader, OTA und CHIP-Spitzenbedarf sind darin nicht
vollständig abgebildet. Die Host-Pakettests prüfen die Komponente, nicht Beken-lwIP
oder den Funkchip. Hersteller-SDKs erzeugen beim Bauen bestehende Compilerwarnungen;
der Build ist kein Warnungsfreiheitsnachweis des gesamten SDKs.

## Verwendete Plattformabhängigkeiten

| Ziel | SDK | lwIP |
| --- | --- | --- |
| BK7231N | BDK 3.0.33, `841343e64d7584a3a650222ab0b6ea967755352a` | 2.2.0-bdk, `0d1cedf` |
| BK7238 | BDK 3.0.78, `44800e7451ea30fbcbd3bb6e905315de59349fee` | 2.2.0-bdk, `0d1cedf` |
| LN882H | `8794085af8f548b4a1f783fdbab36d86aeff9a74` | 2.1.3-ln882h, `7ef5aac` |
| RTL8720CF | AmebaZ2 `5f7b9378a7f226a46e3b13349844a3f5f07565aa` | 2.1.3-ambz2, `929d2dd` |

LibreTiny löst seine SDK-Pakete selbst auf. Das Experiment pinnt ESPHome und den
LibreTiny-Commit, ist aber kein vollständiger Lock aller transitiven Pakete.
Insbesondere der LN882H-SDK-Verweis im Framework ist ein Branch. Bei späteren
Vergleichen diese tatsächlich aufgelösten Versionen erneut festhalten.

## Nicht nachgewiesen

- Boot oder WLAN-/IPv6-Verhalten auf einem physischen Testgerät.
- Multicastübertragung durch einen realen Access Point; eine lokale
  `IPV6_JOIN_GROUP`-Erfolgsmeldung genügt hierfür nicht.
- mDNS/DNS-SD für Matter, PASE, CASE, Attestation, Fabrics, Cluster und Subscriptions.
- Kryptografisch sichere Entropieanbindung auf den Beken-Boards.
- Matter-Flash-/Heapbudget, OTA-Verhalten, Dauerbetrieb oder Stromverbrauch.
- BLE/WLAN-Koexistenz auf T1 oder erfolgreiche Matter-Einrichtung mit einem Controller.

Die zusätzliche GitHub-Actions-Datei beschreibt dieselben Prüfungen. Die Hosttests
und alle vier Firmwarebuilds sind auch im ersten GitHub-Actions-Lauf erfolgreich.
Der übergeordnete Python-Check beanstandete zunächst drei Dateien, weil das
Experiment Ruff-Formatierung mit 100 Zeichen verwendete, das Repository jedoch
Black-Formatierung mit 88 Zeichen prüft. Beide Workflows verwenden jetzt Black
und isort; Ruff bleibt für zusätzliche Codeprüfungen aktiv.

## Hardwareprotokoll für den nächsten Schritt

Pro Gerät Modulname, Chip, ausgelesene Flashgröße, ursprüngliches Partitionslayout,
Buildversionen und verwendetes WLAN erfassen. UART-Log vom Reset bis nach dem
WLAN-Verbindungsaufbau speichern, anschließend Unicast- und Multicast-JSON des
Clients. Nach WLAN-Ausfall und erneutem Einschalten wiederholen. Nach 30 Minuten
Leerlauf und nach belasteten Testphasen erneut den Heap notieren.

`heap_max_block=unavailable` ist beim vorhandenen LibreTiny-API-Fallback ein
erwartbarer fehlender Messwert, keine Blockgröße von null. In diesem Fall ist für
die spätere CHIP-Speicherprüfung eine echte FreeRTOS-Heapstatistik nachzurüsten.
Noch keine Relais-/Lampenfunktion oder Matter-Einrichtung von dieser Firmware erwarten.
