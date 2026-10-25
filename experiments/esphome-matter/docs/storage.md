# Nativer Matter-Speicheradapter

`libretiny_matter_storage` ist der erste implementierte CHIP-Plattformbaustein.
`FlashDBStorage` erbt direkt von der originalen
[`chip::PersistentStorageDelegate`](https://github.com/project-chip/connectedhomeip/blob/4b4a3e405882afaf8161c0e4af244e65fa8f3f94/src/lib/core/CHIPPersistentStorageDelegate.h).
Der Code verwendet echte CHIP-Fehlertypen und FlashDB, keine nachgebildete
Matter-Schnittstelle. Der [native Server](matter-server.md) verwendet diesen
Adapter jetzt für echte Fabric-Zertifikate, Schlüssel und Clusterzustände.

## Einbindung

```yaml
external_components:
  - source:
      type: local
      path: /pfad/zu/libretiny/experiments/esphome-matter/components
    components: [libretiny_matter, libretiny_matter_storage]

libretiny_matter_storage:
  id: matter_storage
```

Für reproduzierbare LibreTiny-Builds dienen `examples/bk7231n-storage.yaml` und
`examples/bk7238-t1-storage.yaml`. Beide verwenden dieselbe Adapterimplementierung.
ESPHome 2026.10.0 und LibreTiny 1.14.0 sind die geprüfte Kombination. Die Anbindung
nutzt ESPHomes derzeit öffentliches `LibreTinyPreferences::db`; Änderungen dieser
Schnittstelle erfordern eine Anpassung. Andere Familien sind noch nicht freigegeben.

`id(matter_storage).get_delegate()` liefert in ESPHome-Lambdas einen
`chip::PersistentStorageDelegate *`. Der rs-matter-Worker nutzt ihn über den
Mainloop-Dispatcher des Servers. Die Speicherkomponente allein öffnet beim
Setup nur den Zugang zur bereits initialisierten Datenbank und schreibt dabei
selbst keine Testwerte. Er initialisiert oder formatiert keine eigene Partition.

## Speichervertrag

| Fall | Verhalten |
| --- | --- |
| Schlüssel | 1–32 Bytes, unverändert hinter `matter/`; keine Hashkollisionen |
| Werte | Binärdaten bis 2.048 Bytes, einschließlich leerer Werte |
| Get mit zu kleinem Puffer | Anfang des Werts kopieren, Anzahl kopierter Bytes zurückgeben, `CHIP_ERROR_BUFFER_TOO_SMALL` |
| Get mit `nullptr`, Länge 0 | Existenzprüfung; leerer vorhandener Wert ist erfolgreich |
| Fehlender Schlüssel | `CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND`, auch beim Löschen |
| Ungültige Parameter | `CHIP_ERROR_INVALID_ARGUMENT`; keine Datenbankänderung |
| Volle FlashDB | `CHIP_ERROR_NO_MEMORY`; kein scheinbar erfolgreicher Schreibvorgang |
| Gemeldeter Flash-Schreib-/Lesefehler | `CHIP_ERROR_PERSISTED_STORAGE_FAILED` |
| Aufruf außerhalb des Eigentümer-Tasks | `CHIP_ERROR_INCORRECT_STATE` vor Datenbankzugriff |
| ESPHome-Factory-Reset/deinitialisierte DB | Weitere Zugriffe werden abgewiesen; gemeinsame Partition bedeutet gemeinsamen Reset |

Das Präfix trennt Matter-Schlüssel von ESPHomes numerischen Preference-Schlüsseln.
Die 2-KiB-Grenze lässt Platz für Schlüssel und FlashDB-Header innerhalb der
4-KiB-Sektoren. Sie ist eine dokumentierte Grenze dieses ersten Backends, keine
Aussage über sämtliche Größenanforderungen eines vollständigen Matter-Servers.
Die vorhandene Partitionsgröße wird nicht verändert; ESPHome und Matter teilen
sich ihre Kapazität. Es gibt keinen eigenen Heap-Puffer oder eine verzögerte
Schreibwarteschlange im Adapter.

## Tasks, Fehler und Lebenszyklus

ESPHome besitzt die Datenbank. Alle Adapterzugriffe müssen im ESPHome-Haupttask
laufen, der beim Setup erfasst wird. Diese Beschränkung wird zur Laufzeit geprüft.
Direkte Aufrufe aus einem Worker bleiben gesperrt. Der native Server übergibt
Aufträge über eine einplatzige Queue an den Mainloop und wartet auf Bestätigung.
So nutzen auch ESPHomes vorhandene Preferences dieselbe Task-Grenze; ein separater
Mutex nur für Matter wäre dafür nicht ausreichend.

Nach einem Reset gehört auch die Bindung zum alten Lebenszyklus: Die Komponente
wird erst beim nächsten Boot neu eingerichtet. `FlashDBStorage` darf nicht erneut
an eine andere Datenbank gebunden werden. Datenbank und Kontext müssen den Delegate
überleben. Aufrufe aus Interrupts sind nicht unterstützt.

FlashDB 1.2.0 propagiert Fehler beim Nachschlagen von Metadaten nicht durchgängig.
Ein durch CRC verworfener Eintrag kann deshalb wie ein fehlender Schlüssel wirken.
Der Adapter prüft ein zurückgegebenes CRC-Ergebnis und den eigentlichen Blob-Read,
kann aber die fehlende Fehlerunterscheidung des Backends nicht nachträglich
herstellen. Der Entwicklungsserver speichert bereits Credentials; für belastbaren
Produktbetrieb fehlen ein Audit dieser Backendpfade, physische Stromausfalltests
und ein abgestimmter benutzerseitiger Matter-Reset. FlashDB bietet
hier keine Verschlüsselung ruhender Daten und keine Transaktion über mehrere Keys.

## Wiederverwendeter Code

- Unveränderte CHIP-Header aus `project-chip/connectedhomeip`, Commit
  `4b4a3e405882afaf8161c0e4af244e65fa8f3f94`, Apache-2.0. Delegate und Fehlertypen
  liegen zusammen mit den benötigten Codecs unter
  `components/libretiny_matter/vendor/connectedhomeip`; beide Komponenten verwenden
  denselben Upstream-Stand. Siehe auch [Protokolltests](protocol-tests.md).
- `upstream.json` enthält Ursprung und SHA-256 jeder übernommenen Datei. Die
  Tests prüfen diese Prüfsummen. Upstream-Formatierung und Copyright bleiben erhalten.
- LibreTinys FlashDB 1.2.0, Commit `d5c892f0c545884e9f0de0cf67215e426dc8109d`.
  Firmware nutzt das vorhandene Framework-Paket; `tools/fetch_flashdb.py` lädt
  denselben Stand für Hosttests. Keine zweite FlashDB wird in die Firmware gelinkt.

Die C++-Quellen werden als lokale PlatformIO-Bibliothek eingebunden. Der Server
verwendet rs-matter hinter einer schmalen C ABI; er bindet keine zweite C++-CHIP-
Version ein. Beide Versionen und ihre verschiedenen Aufgaben sind in
[architecture.md](architecture.md) dokumentiert.

## Tests und nächster Schritt

Die Hosttests kompilieren den produktiven Adapter, die Original-CHIP-Header und
FlashDB-C-Quellen. Nur die FAL-Hardwaregrenze wird durch einen 32-KiB-NOR-Simulator
ersetzt: Löschen setzt Bytes auf `0xff`, Programmieren erlaubt nur 1→0-Übergänge.
Ein gespeichertes Flashabbild wird in einem zweiten Prozess geladen, um Persistenz
ohne Übernahme des RAM-Caches zu prüfen. Weitere Fälle decken den Speichervertrag,
Fehlschreibvorgänge, fehlgeschlagene Blob-Reads, volle Partition mit anschließender
Speicherfreigabe, Namespace-Isolation und Aufrufe aus einem falschen Task ab.

```sh
python tools/fetch_flashdb.py
pytest -q tests/test_storage.py
```

Die Tests ersetzen keinen Stromausfall während eines realen Flashschreibvorgangs.
Die zusätzlichen Servertests verwenden dieselbe FlashDB und denselben NOR-
Simulator: Kopplung, echte Fabric-Daten, CASE nach Prozessneustart und Ablehnung
von CommissioningComplete bei einem injizierten Fabric-Schreibfehler. Diese Tests
sind in [matter-server.md](matter-server.md) beschrieben.
