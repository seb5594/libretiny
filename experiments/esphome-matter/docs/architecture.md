# Zielarchitektur und nachprüfbare Meilensteine

Diagnosekomponente, nativer FlashDB-Adapter und originale CHIP-Codecs für TLV und
InvokeRequest-Nutzdaten sind eingebunden. Die Codecs sind auf dem Host getestet
und als Selbsttest auf den Ziel-MCUs baubar. Die übrigen Schichten sind geplant;
es gibt noch keinen Matter-Server.

## Grenzen der Schichten

```text
ESPHome-YAML und vorhandene Entities (switch/light/sensor)
    |
    v
gemeinsames Matter-Schema und Endpoint-/Cluster-Zuordnung
    |
    v
Matter-Backend / connectedhomeip
    |
    v
LibreTiny-Plattformanbindung
    |-- Zeit, Task und Synchronisation -> vorhandenes FreeRTOS
    |-- IPv6, UDP, Multicast           -> vorhandenes lwIP
    |-- Wi-Fi-Ereignisse               -> ESPHome / LibreTiny
    |-- persistente Fabric-Daten       -> geprüfter FlashDB/KVS-Adapter
    |-- Entropie und Crypto            -> geprüfter TRNG-/mbedTLS-Adapter
    `-- Board/Partitionen              -> LibreTiny-Profil
```

Das gemeinsame Python-Schema soll wie ESPHomes Zigbee-Integration beim Generieren
des Codes das Backend auswählen. Entity-Zuordnung und Validierung gehören nicht
in `#ifdef BK7231N`-Blöcke. SDK-Unterschiede von BK7231N und BK7238 werden unterhalb
des Backends gekapselt. Erweiterungen für LN882H/RTL8720C folgen derselben Grenze.

Die vorhandene `esp-matter`-Komponente kann als Referenz für das Datenmodell
dienen. Ihren ESP-IDF-Unterbau unverändert in LibreTiny zu kompilieren ist kein
tragfähiger Integrationsweg. Wiederverwendbarer Code wird erst nach Prüfung seiner
Lizenz und tatsächlichen Plattformabhängigkeiten übernommen. Cryptografie,
PASE/CASE, TLV und das Interaction Model werden nicht neu erfunden.

## Meilensteine

| Schritt | Umsetzung | Nachweis für Abschluss |
| --- | --- | --- |
| 0 — vorhanden | Diagnosekomponente, LibreTiny-Builds, IPv6-Client, Hosttests | Lokale Ergebnisse in `validation.md`; Hardwaremessungen noch offen |
| 0a — vorhanden | CHIP-Speicherschnittstelle auf bestehender ESPHome-FlashDB, Beken-Builds | [Speicheradapter](storage.md): Vertrag, Fehlerfälle und Prozessneustart mit echter FlashDB getestet; Hardware-Stromausfalltest offen |
| 0b — vorhanden | Native CHIP-TLV-/InvokeRequest-Codecs und Selbsttest-Package für bestehende Geräte | [Protokolltests](protocol-tests.md): feste Bytevektoren, Grenzfälle, Sanitizer, begrenztes Fuzzing; MCU-Laufzeitlogs offen |
| 1 | Beken-IPv6 einschließlich mDNS/Multicast und Speicher erfassen | UART-Logs, Unicast-/Multicastantworten, WLAN-Reconnect, Langzeitlauf auf N und T1 |
| 2 | CHIP mit ARMv5TE-Toolchain in LibreTiny einbinden; Entropie, Zeit, KVS und Task anbinden | Verlinkter Server, nachvollziehbare Lizenz-/Versionsliste, getestete Entropiefehler und persistente Fabric-Daten |
| 3 | Ein On/Off-Endpunkt, vorhandenes WLAN, kleiner begrenzter Ressourcenbedarf | Pairing, On/Off, Attribute-Read/Subscribe, Reboot und Wiederverbindung mit einem Controller |
| 4 | Mehrere Fabrics und Ressourcenoptimierung | Zweiter Controller, Rechteprüfung, gleichzeitige Subscriptions, gemessene PASE-/CASE-Spitzen und keine Speicherlecks |
| 5 | Weitere Entities, Aktualisierungspfad, optionale BLE-Einrichtung | Gerätetypentests, unterbrochene Updates, konsistenter Factory Reset; T1-BLE separat nachweisen |

Für Schritt 2 ist ein eigener `platform/libretiny`-Adapter sinnvoll, der den
passenden Beken-Code wiederverwendet. Der offizielle Beken-Port und das ARM-SDK
sind unterschiedliche Integrationsbasen und müssen anhand einer festgelegten
CHIP-Version verglichen werden. Ein pauschaler Austausch von ESP-IDF-Includes
oder ein ungetesteter Backend-Stub wäre kein fertiggestellter Schritt.

Der vorhandene Speicheradapter ist ein Teil von Schritt 2, kein Abschluss davon.
Er erlaubt Zugriffe nur aus dem ESPHome-Haupttask. Bevor ein CHIP-Worker eingeführt
wird, muss die gemeinsame Datenbank über alle Leser/Schreiber hinweg synchronisiert
werden. Ein Mutex nur um die drei neuen Delegate-Methoden würde ESPHomes vorhandene
Preferences-Zugriffe nicht schützen.

## Regeln für den nativen Port

- ESPHome besitzt den WLAN-Lebenszyklus. CHIP darf ihn nicht zusätzlich aus
  einer eigenen Beispielanwendung initialisieren oder zurücksetzen.
- CHIP-Callbacks und ESPHome-Mainloop müssen sich Zustandsänderungen über einen
  klar definierten, begrenzten Mechanismus übergeben. Keine GPIO-Aktionen direkt
  aus fremden Tasks und keine Flashoperationen in Netzwerkcallbacks.
- Fabric-Daten werden mit vollständigen Schlüsseln und geprüfter Persistenz
  gespeichert. Eine nur gehashte ESPHome-Preference-ID reicht als allgemeiner
  Matter-Key-Value-Store nicht. Stromausfall, volle Partition, Löschen und
  Teil-Lesezugriffe benötigen Tests.
- Initiale Einrichtung über das bestehende LAN; ein begrenztes Commissioning-
  Fenster. Nach erfolgreicher Einrichtung bleiben Fabric-Daten über Neustarts
  erhalten. Multi-Admin, erneute Freigabe und Factory Reset werden ausdrücklich
  implementiert, nicht durch „beim Start alles löschen“ ersetzt.
- Test-VID/PID und Entwicklungszertifikate sind für den Laborprototyp zu markieren.
  Ein Produkt benötigt eigene Attestation-/Provisioning-Prozesse und die jeweils
  erforderliche Konformitätsprüfung. Keine echten privaten Geräteschlüssel ins Repo.
- Für das knappe Beken-Budget zunächst nur benötigte Gerätetypen und Cluster
  einbinden. BLE, umfangreiche Logs, Webserver und zusätzliche APIs getrennt
  vermessen. Linker-Garbage-Collection und freigegebene Optimierung nutzen; die
  LibreTiny-Warnung vor beliebigen Änderungen an der SDK-Optimierungsstufe beachten.
- OTA-Speicher und Wiederherstellbarkeit bleiben Teil des Budgets. Eine größere
  Apppartition ist keine kostenlose Performanceoptimierung. Die T1-RF-Daten
  dürfen keinesfalls mit den Offsets eines anderen SDKs überschrieben werden.

## Performance-Messung

Pro Board und Buildprofil erfassen: Flashbelegung, statische RAM-Segmente,
Task-Stack-Maxima, freier/minimaler Heap, größter freier Block, Zeit für PASE und
CASE, Befehlslatenz (Median/p95/p99), Reconnectdauer und Verbrauch über 24 Stunden.
Das ursprüngliche Geräteprofil und die Minimalfirmware werden getrennt verglichen.
Alle Angaben benötigen die Softwareversionen und das konkrete Modul.

Ein Budgetentscheid fällt anhand der höchsten beobachteten Laufzeitlast plus
Reserve. Erfolgreiche Hosttests und ein kleiner Minimalbuild alleine erlauben
keine Aussage über den Platzbedarf eines vollständigen Matter-Geräts.
