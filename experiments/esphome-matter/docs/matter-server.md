# Nativen Matter-Server testen

## Bestehendes ESPHome-Gerät

Voraussetzung: ESPHome 2026.10.0, BK7231N oder BK7238/T1, offizielles LibreTiny 1.14.0,
vorhandene WLAN-Konfiguration. Der Buildrechner braucht Rustup und
`nightly-2026-10-24` mit `rust-src` wie im [README](../README.md) beschrieben.
Eine normale ESPHome-Dashboard-Installation enthält diese Toolchain meist noch nicht;
für den ersten Test die dokumentierte lokale CLI verwenden.

Im bestehenden YAML ergänzen:

```yaml
packages:
  matter_test: github://seb5594/libretiny/experiments/esphome-matter/examples/existing-device-matter-package.yaml@feat/esphome-matter-feasibility
```

In der vorhandenen `secrets.yaml` einen eigenen gültigen Einrichtungscode ergänzen:

```yaml
matter_passcode: 34567890
```

Das Package erhält Board, Framework, WLAN und GPIO-Konfiguration. Es aktiviert IPv6,
wartet auf mindestens eine IPv6-Adresse und deaktiviert ESPHomes mDNS-Responder.
API/OTA sind weiter über die **IP-Adresse** erreichbar, nicht per ESPHome-`.local`-
Erkennung. Eine bereits vorhandene `external_components`-Konfiguration auf doppelte
Einträge prüfen. Bei zusammengeführten Packages darf `mdns.disabled` nicht wieder
überschrieben werden; die Schema-Validierung kontrolliert das.

Das Package fügt ein **virtuelles** Licht an Matter-Endpunkt 1 hinzu. Bestehende
Relais werden nicht mit diesem Licht verbunden. Zum Vergleich mit üblichen
Bestandskonfigurationen bauen die beiden `*-matter-existing.yaml`-Profile zusätzlich
ESPHome-API und OTA. Andere Gerätefunktionen verbrauchen weiteren Flash/Heap; ein
passendes Minimalimage beweist deshalb nicht, dass jede bestehende Anwendung passt.

Zum reproduzierbaren Einsatz nach einem erfolgreichen Test den Branch-Namen im Package
und dessen `external_components`-Quelle auf den getesteten Commit pinnen.

## Koppeln und bedienen

Der Server öffnet beim Start ohne gespeicherte Fabric ein 300-Sekunden-Fenster.
Nach erfolgreicher Kopplung öffnet ein Neustart kein neues Fenster. Die
Administrator-Commissioning-Cluster kommen aus rs-matter; zusätzliche Fabrics und
Ökosysteme sind noch nicht als Testmatrix verifiziert.

Einrichtung über das vorhandene LAN, ohne BLE und ohne Internet-IPv6. Gerät und
Controller müssen sich per IPv6 und mDNS erreichen; WLAN-Client-Isolation und
Multicastfilter können das verhindern. Verwendet werden UDP/5540 und UDP/5353.

Mit einem gebauten offiziellen `chip-tool` und den Testprofilen:

```sh
chip-tool pairing onnetwork 305414945 20202021
chip-tool onoff on 305414945 1
chip-tool onoff read on-off 305414945 1
chip-tool onoff off 305414945 1
chip-tool onoff toggle 305414945 1
chip-tool onoff subscribe on-off 0 40 305414945 1
```

Bei eigenem `matter_passcode` den Code ersetzen. Zur gezielten Geräteauswahl kann
`pairing onnetwork-long 305414945 <Code> 3840` nach Discriminator filtern; die
genauen Optionen hängen von der verwendeten chip-tool-Version ab.
Der automatisierte Python-Test unten überprüft die Attestation gegen den
beigelegten öffentlichen Test-PAA. Andere Controller müssen Entwicklungsgeräte
mit Test-VID `0xFFF1`, PID `0x8001` und Test-Attestation akzeptieren. Eine automatische
Freigabe durch Apple, Google, Alexa oder Home Assistant wird nicht behauptet.

WLAN-Zugangsdaten bleiben in ESPHome. Matter-Scan-/Connect-Kommandos sind in diesem
Port nicht implementiert und antworten mit `InvalidAction`. Matter-OTA ist nicht
vorhanden; Updates erfolgen über den bisherigen ESPHome-/LibreTiny-Weg.

## Erwartete UART-Logs und Hardwareprüfung

Die Reihenfolge beim ersten Start:

1. ESPHome startet WLAN und erhält eine gültige IPv6-Adresse.
2. `Starting Matter: heap=... stack=24576 interface=...`
3. `Loading fabric credentials and Matter state`
4. `Matter endpoint ready; commissioned=false`
5. `On-network commissioning open for 300 seconds`
6. `Starting UDP/5540, mDNS, secure sessions and subscriptions`
7. PASE, Attestation, CSR, AddNOC und CommissioningComplete; danach OnOff-Änderungen.

Alle 30 Sekunden: Verbindungsstatus, virtueller Schaltzustand, freier/minimaler
Heap, langsamster Poll und `stack_min_free`. Die FlashDB-Anbindung loggt Operation,
Schlüssel und Größe; keine gespeicherten Zugangsdaten. Rust-Logs sind maximal INFO,
ESPHome-Logs im Testprofil DEBUG. Kein TLV-/Schlüssel-Dump wird aktiviert.

Erster Test: koppeln, viermal On/Off/Toggle, Read und Subscribe; anschließend mindestens
vier Sekunden warten, neu starten und ohne erneute Kopplung lesen. Danach WLAN kurz
unterbrechen und denselben Test wiederholen. Der Worker schließt seine Sitzungen bei
Adress-/Verbindungswechsel und lädt die gespeicherten Fabrics beim Wiederanlauf.
Ein Verlust nur von IPv6 führt ebenfalls zur Pause. Bei wiederholten internen Fehlern
stoppt Matter, während ESPHome weiterläuft.

Bitte UART-Log ab Boot, genaues Modul, Build-Commit und Controller-Version zusammen
mit Heap-/Stack-Werten festhalten. PASE/CASE-Zeit und 24-Stunden-Verhalten sind noch
nicht auf ARM9 gemessen. Der Host-Warmstart testet den Rust-Lebenszyklus, nicht den
Beken-WLAN-Treiber. Die Komponente veröffentlicht derzeit die erste gültige IPv6-Adresse
und IPv4 des STA-Interfaces; komplexe Netze mit mehreren IPv6-Präfixen sind ungetestet.

Es kann mehr als ein Absturz passieren: unvollständige Flash-Schreibvorgänge können
Einstellungen beschädigen und UART-Wiederherstellung erfordern. Die virtuelle
Testleuchte löst keine Relais aus. Bei einer bestehenden Anwendung wirken deren
Boot-/Restore-Regeln bei jedem Neustart weiter. Ein isoliertes Testgerät mit Backup
ist deshalb der passende erste Aufbau.

## Unabhängige Interoperabilitätstests

Auf einem Linux-Rechner mit Python 3.12 (in der Entwicklungs-venv):

```sh
pip install -r tests/server/requirements.txt
# Debian/Ubuntu: libnl-route-3-200 und libglib2.0 installieren.
# Die native CHIP-Linux-Plattform braucht ein für diesen Testbenutzer beschreibbares /data.
# Dafür einen isolierten Testrechner/Container verwenden.
python tools/fetch_flashdb.py
python tools/build_matter_host.py --build-dir .cache/server-build
python tools/test_matter_server.py \
  --binary .cache/server-build/matter-host \
  --build-dir .cache/interop-run-1 \
  --ipv4 <LAN-IPv4-des-Testrechners>
```

`--build-dir` muss neu sein, damit vorhandene Fabric-Daten nicht überschrieben werden.
Die Ports 5353/5540 müssen verfügbar sein. Der Host veröffentlicht `::1` und seine
LAN-IPv4; Controller und Hostserver laufen deshalb auf demselben Rechner.

Geprüft werden mDNS-Erkennung, falscher PIN, komplette Kopplung mit PAA-Prüfung,
On/Off/Toggle/Read, Subscription, BasicInformation, persistiertes NodeLabel per
WriteAttribute, Schreibschutz des OnOff-Attributs, ACL-Ablehnung eines zweiten
Controllers ohne Grant, falscher Endpunkt und 98 fehlerhafte UDP-Datagramme mit
anschließendem gültigem Read. Danach folgen Prozessneustart, Warmstart, ein echter
NOR-Schreibfehler beim Fabric-Speichern, erneute Kopplung nach dessen Neustart und
Ausfall der Entropiequelle. Ein Entropiefehler muss den Stack vor der Einrichtung
stoppen; es gibt keinen `rand()`-Fallback.

`summary.json` enthält nur Prüfergebnisse. Controller-JSON, Flashabbilder und rohe
Controllerlogs bleiben lokal: sie enthalten Entwicklungszugangsdaten und gehören
nicht in einen Commit oder öffentliche CI-Artefakte.

Optional kann der Controller isoliert in `tests/server/Dockerfile` laufen. Das Image
enthält nur die Systembibliotheken; die separate Python-Umgebung mit den gepinnten
Controllerpaketen wird lesbar eingebunden. Der Docker-Build erwartet das BuildKit-
Secret `ca_bundle` für die Zertifikatskette der jeweiligen Umgebung. Der Testläufer
unterstützt `--controller-container <Image>` und
`--controller-site-packages <absoluter-Pfad>`. Hostnetzwerk ist für die lokale
mDNS-/IPv6-Interoperabilität erforderlich. GitHub Actions nutzt einen eigenen
Ubuntu-Runner und benötigt diesen Dockerweg nicht.
