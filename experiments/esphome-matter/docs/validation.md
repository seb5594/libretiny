# Prüfstand vom 24.10.2026

## Lokal erfolgreich

- ESPHome 2026.10.0, Python 3.12.14, LibreTiny 1.14.0 aus
  `seb5594/libretiny@c81bc9cef83ffd42fcffce32b8b08793b2400b9f`.
- Vier Diagnoseprofile, zwei Beken-Speicherprofile und vier Codec-Testprofile
  vollständig kompiliert und verlinkt; UF2-Dateien erzeugt. Die Codec-Testprofile
  verwenden die offizielle LibreTiny-Version 1.14.0.
- ESPHome-Hostprogramm mit demselben Probe-C++-Code kompiliert und ausgeführt.
- 65 pytest-Prüfungen bestanden: echte IPv6-Roundtrips, fehlender Listener,
  verworfene kurze/falsche/übergroße Datagramme mit anschließendem gültigem
  Roundtrip, IPv6-Scopevalidierung, ungültige ESPHome-Konfigurationen und die neuen
  Speicherprüfungen mit originalen CHIP-Headern und echter FlashDB sowie die neuen
  Codec-Prüfungen. Die tatsächliche ESPHome-Hostkomponente meldet 72 erfolgreiche
  Codec-Einzelprüfungen. Zusätzliche Parser-Tests laufen mit AddressSanitizer und
  UndefinedBehaviorSanitizer, einschließlich 20.000 reproduzierbarer Zufallseingaben.
- Ruff, autoflake, Black, isort und clang-format erfolgreich. Die Python-Formatierung
  folgt den Black-/isort-Vorgaben des übergeordneten LibreTiny-Repositories.

| Profil | IPv6 / Echo | Flash laut Build | ausgewiesenes Flashlimit | statisches RAM laut Build |
| --- | --- | ---: | ---: | ---: |
| BK7231N Tuya | einkompiliert | 500.794 B | 1.083.136 B | 47.620 B |
| BK7238 Tuya T1 | einkompiliert | 601.344 B | 1.083.136 B | 51.528 B |
| LN882H | aus | 409.816 B | 1.228.800 B | 94.332 B |
| RTL8720CF 2M/992k | aus | 435.769 B | 1.015.808 B | 12.841 B |
| BK7231N + CHIP-Speicheradapter | einkompiliert | 502.490 B | 1.083.136 B | 47.644 B |
| BK7238/T1 + CHIP-Speicheradapter | einkompiliert | 603.032 B | 1.083.136 B | 51.552 B |
| BK7231N + Codec-Selbsttest | einkompiliert | 508.878 B | 1.083.136 B | 47.684 B |
| BK7238/T1 + Codec-Selbsttest | einkompiliert | 609.416 B | 1.083.136 B | 51.592 B |
| LN882H + Codec-Selbsttest | aus | 417.792 B | 1.228.800 B | 94.400 B |
| RTL8720CF + Codec-Selbsttest | aus | 443.117 B | 1.015.808 B | 12.841 B |

Das sind **Firmwarewerte ohne vollständigen Matter-Stack**. Die letzten beiden
Speicherprofile enthalten den echten CHIP-Speicheradapter; der Mehrbedarf gegenüber
den Diagnoseprofilen liegt bei rund 1,7 KiB Flash. Die Codec-Testprofile enthalten
originale TLV-/InvokeRequest-Codecs, Selbsttests und den Diagnosebutton, mit rund
7–8 KiB zusätzlichem Flashbedarf. Das Buildsystem bewertet je nach
Plattform unterschiedliche Speichersegmente; insbesondere die kleinen statischen
RTL-Zahlen dürfen nicht als Gesamtbedarf interpretiert werden. Laufzeit-Heap,
Wi-Fi-/RTOS-Reservierungen, Bootloader, OTA und CHIP-Spitzenbedarf sind darin nicht
vollständig abgebildet. Die Host-Pakettests prüfen die Komponente, nicht Beken-lwIP
oder den Funkchip. Hersteller-SDKs erzeugen beim Bauen bestehende Compilerwarnungen;
der Build ist kein Warnungsfreiheitsnachweis des gesamten SDKs.

Die Speichertests prüfen Binärdaten, leere Werte und deren Überschreiben,
32-Byte-Schlüssel, 2-KiB-Werte, ungültige Parameter, Teil-Lesezugriffe, Existenz und
Löschen, Isolation numerischer ESPHome-Schlüssel, Aufrufe aus einem fremden Task
sowie Zugriffe vor Bindung/nach Deinitialisierung. Der NOR-Simulator erzwingt
1→0-Programmierung und 4-KiB-Löschgrenzen. Fehler werden für neue und bestehende
Werte, Löschen und Blob-Lesen injiziert; CRC-Fehler eines gecachten Eintrags werden
erkannt. Eine volle 32-KiB-Partition wird geprüft, dann Speicher durch Löschen und
FlashDB-Garbage-Collection wiederverwendet. Ein separater Prozess liest ein
persistiertes Flashabbild einschließlich leerem und gelöschtem Schlüssel.
Die Testfälle enthalten keine echten Fabric-Schlüssel oder Zertifikate.
Die Codec-Tests verwenden bekannte Off-/On-/Toggle-Nutzdaten. Sie enthalten keine
verschlüsselten Sessions oder Hardwarezugriffe. Details und das Package für
vorhandene Geräte stehen in [protocol-tests.md](protocol-tests.md).

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
- mDNS/DNS-SD für Matter, PASE, CASE, Attestation, echte Fabrics, Cluster-Server
  und Subscriptions. Das Lesen/Schreiben von InvokeRequest-Nutzdaten ist separat
  durch Codec-Tests belegt und ersetzt diese fehlenden Schichten nicht.
- Kryptografisch sichere Entropieanbindung auf den Beken-Boards.
- Stromausfallsicherheit während Schreib-/Löschvorgängen und vollständige
  Fehlerpropagierung der FlashDB-Metadatenpfade; Grenzen in [storage.md](storage.md).
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
