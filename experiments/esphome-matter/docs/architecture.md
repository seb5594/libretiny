# Architektur des nativen Matter-Ports

Die aktive Serverimplementierung verwendet **Project CHIP rs-matter** statt einen
ESP-IDF-Port oder die fremde BK7238-SDK-Firmware. Protokoll, Kryptografie und
Clusterzustände stammen aus dem gepinnten Upstream. Die Plattformanbindung ist klein
und getrennt von ESPHomes Python-Schema und dem Matter-Datenmodell.

```text
ESPHome YAML -> Validierung / Codegen
    |
MatterServer: ESPHome-Mainloop
    |-- bestehendes WLAN und IPv6 / Adressänderungen
    |-- FlashDB-Delegate im selben Task wie ESPHome-Preferences
    |-- Queue (1 Request) + Semaphore
    |
FreeRTOS-Matter-Worker (24 KiB Stack, Priorität 1)
    |-- C ABI: TRNG, Zeit, Allocator, Logs, Storage, UDP
    |-- rs-matter: PASE/CASE, MRP, Fabric/ACL, Interaction Model
    |-- Root-Endpoint: WLAN-Diagnose, Administration, Attestation
    `-- Endpoint 1: Descriptor, Identify, Groups, OnOff-Licht
```

Der Server berührt keine GPIOs. Der virtuelle OnOff-Zustand wird unter einer
Critical Section an den Mainloop übergeben. Eine spätere Entity-Zuordnung gehört
in diese ESPHome-Schicht; netzwerkspezifische Details gehören weiter hinter die C ABI.
Das entspricht der Trennung gemeinsamer Konfiguration und Plattformbackends in
ESPHomes Zigbee-Komponente, ohne deren nicht austauschbares Funkprotokoll zu kopieren.

## Plattformvertrag

- ESPHome besitzt WLAN-Zugangsdaten und Reconnect. Der Server wartet auf gültiges IPv6.
  Adressänderungen beenden laufende Sessions; der Stack startet mit gespeicherten
  Fabrics neu. Matter-WLAN-Scan/Netzwechsel werden explizit abgelehnt.
- Zwei nicht blockierende Dualstack-UDP-Sockets: Matter 5540 und mDNS 5353.
  Receive-Buffer begrenzt, übergroße Pakete verworfen, keine zusätzliche Paketqueue.
  Der Rust-Empfänger gibt zwischen Datagrammen an den Executor zurück. Dieser pollt
  alle 10 ms; Send-Wiederholungen bei `EAGAIN` sind begrenzt.
- Eine einplatzige Queue übergibt Speicheraufträge vom Worker an den ESPHome-Mainloop.
  Der Worker wartet bis zur Bestätigung, damit kein auslaufender Stackpointer im
  Mainloop verbleibt. Alle gemeinsamen FlashDB-Zugriffe bleiben im Mainloop.
- FlashDB verwendet vorhandene Partition und Schlüssel `matter/rs/xxxx`, bis 2 KiB
  pro Wert. Der bereits geprüfte C++-CHIP-Delegate wird wiederverwendet.
  Keine separate ungeprüfte KV-Implementierung; keine neue Flashpartition.
- Entropie über Beken `sddev_control(IRDA_DEV_NAME, TRNG_CMD_GET, ...)`, wortweise mit
  Fehlerkontrolle. Drei identische aufeinanderfolgende Wörter stoppen Matter.
  Dieser Kontinuitätstest ist keine Entropiezertifizierung. Der alte BDK-Bytehelper
  und `lt_rand_bytes()`/`rand()` werden nicht für Matter verwendet.
- FreeRTOS übernimmt Heap und Critical Sections. Die Uhr erweitert den
  Millisekundenzähler über Wraparound. Rust-Panics/Entropiefehler stoppen den Worker;
  ESPHome bleibt aktiv. Das stellt keinen Schutz gegen Hardware-Stacküberläufe dar.
- Statische Protokollzustände werden direkt am Zielort initialisiert. Große temporäre
  Stackkopien werden vermieden. Warmstop droppt den Future vor den geliehenen
  Protokollzuständen; init/poll/stop laufen ausschließlich im selben Worker.

## Reproduzierbare Abhängigkeiten

`rs-matter` 0.4.1, Commit `7195e2b1f45df94ee1bc63605b7db91a6878b9dc`,
Apache-2.0, implementiert Matter-1.6-Schemata. Der getestete Controller ist
ConnectedHomeIP aus `home-assistant-chip-core/clusters==2025.7.0`.
Nightly-Rust `2026-10-24` baut `core`/`alloc` für `thumbv5te-none-eabi`.
`Cargo.lock` pinnt transitive Abhängigkeiten; `cargo --locked` verhindert stille Updates.

Zwei eng begrenzte lokale Upstream-Anpassungen liegen unter `rust/vendor`:

1. `embassy-sync` verwendet in `LazyLock`/`OnceLock` portable atomics statt der auf
   ARMv5TE fehlenden Core-Atomics. Critical Sections kommen aus FreeRTOS.
2. `rs-matter-codegen` erzeugt nur die 53 von den kompilierten Modulen referenzierten
   Clusterdeklarationen. Alle genutzten Felder/Kommandos bleiben unverändert. Ohne
   Filter erzeugt Upstream über 30 MB Rust und überschreitet das Compiler-RAM-Budget.

Manifestdateien dokumentieren Originalchecksummen und geänderte Dateien. Tests
prüfen unveränderte Dateien. Lizenzen bleiben erhalten. Kryptografische Algorithmen
und Protokollimplementierungen wurden nicht lokal neu geschrieben.

## Nachweis und verbleibende Arbeit

Softwareseitig nachgewiesen: sichere Kopplung, Attestation mit öffentlichen
Testzertifikaten, NOC/Fabric-Persistenz, CASE nach Neustart, OnOff/Read/Subscribe,
ACL-Verweigerung, ungültige Pakete, Speicher-/Entropiefehler, Warmstart und Builds
für beide Beken-Ziele, einschließlich API/OTA-Vergleichsprofilen.

Noch offen: tatsächlicher Beken-Funkbetrieb und Ressourcenmessung, physischer
Stromausfall beim Flashen, Multi-Fabric-/Gruppenlast, mehrere gleichzeitige
Subscriptions, Produkt-Attestation, Entity-Mapping, benutzerseitiger Reset und
optionales BLE-Provisioning. LN882H/RTL8720C brauchen zusätzliche IPv6-/Plattformarbeit.

Performancewerte dürfen erst aus MCU-Messungen abgeleitet werden: Flash, statisches
RAM, Stack-Minimum, Heap-Minimum, PASE/CASE-Dauer, Befehlslatenz und 24-Stunden-Verlauf.
Ein erfolgreicher Link beweist weder verbleibenden Laufzeit-Heap noch ausreichende
ARM9-Kryptografieleistung.
