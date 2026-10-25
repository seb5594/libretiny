# Matter-Nachrichten auf vorhandenen ESPHome-Geräten testen

Dieser Abschnitt beschreibt die separaten Codec-Selbsttests. Für den inzwischen
implementierten Netzwerkserver mit Pairing siehe [Matter-Server](matter-server.md).

Die Komponente `libretiny_matter_protocol` baut originale Matter-Codecs unter
LibreTiny und führt dieselben 72 Prüfungen auf dem Host und auf der MCU aus.
Sie erzeugt und liest echte **Interaction-Model-Nutzdaten für Off, On und Toggle**
mit den unveränderten CHIP-Klassen `InvokeRequestMessage`, `CommandDataIB`,
`CommandPathIB`, `TLVReader` und `TLVWriter`.

**Das ist noch kein koppelbares Matter-Gerät.** Geprüft wird die Verarbeitung der
Nachrichtennutzdaten. Verschlüsselte UDP-Pakete, PASE/CASE, Controller-Verbindungen,
mDNS, Zugriffsrechte, Subscriptions und Relaissteuerung sind noch nicht angebunden.
Der Test öffnet keinen Netzwerkport und schreibt keine Flash-Testdaten.

## Zu einer bestehenden Konfiguration hinzufügen

In der vorhandenen Geräte-YAML diese Package-Zeile ergänzen. Falls `packages:`
bereits existiert, darunter nur den neuen Eintrag hinzufügen:

```yaml
packages:
  matter_protocol_test: github://seb5594/libretiny/experiments/esphome-matter/examples/existing-device-package.yaml@feat/esphome-matter-feasibility
```

Das Package ergänzt die externen Komponenten und einen Diagnosebutton. Es setzt
kein Board, keine Pins, keine WLAN-Daten und keine Frameworkquelle. Deine bestehende
Konfiguration kann weiter die **offizielle LibreTiny-Plattform** verwenden.
Geprüfte Versionen: ESPHome 2026.10.0 und LibreTiny 1.14.0. Der Branch ist ein
Entwicklungsstand; für einen dauerhaft reproduzierbaren Aufbau auf einen geprüften
Commit pinnen.

1. Geräte-YAML zunächst kompilieren und den ausgewiesenen Flashbedarf prüfen.
2. Mit dem für dieses Gerät bereits eingerichteten ESPHome-Verfahren installieren.
3. Nach dem Neustart ESPHome-Logs öffnen. Erwartete Meldung:

   ```text
   [libretiny_matter_protocol] Matter codec tests: passed=72 failed=0 duration=... ms
   ```

4. Über den Diagnosebutton **Matter codec self-test** lässt sich der Test erneut
   ausführen. Der Button erscheint über die vorhandene ESPHome-API; ohne API bleibt
   der automatische Test mit Logausgabe beim Boot verfügbar.

Bei einem Fehler werden Anzahl und erster fehlgeschlagener Test geloggt. Für die
weitere Entwicklung sind Chip-/Modulname, ESPHome-/LibreTiny-Version, diese Meldung,
Laufzeit und Build-Speicherwerte hilfreich. Ein positives Ergebnis bestätigt die
Codecs auf diesem Build; es bestätigt noch keine Matter-Kopplung oder Funkübertragung.

Mit `libretiny_matter_protocol: {run_on_boot: false}` im Geräteprofil lässt sich
der automatische Start abschalten; der Diagnosebutton bleibt nutzbar.

## Eigenständige Buildbeispiele

Die folgenden Profile verwenden explizit die offizielle LibreTiny-Version 1.14.0:

```sh
esphome compile examples/bk7231n-protocol.yaml
esphome compile examples/bk7238-t1-protocol.yaml
esphome compile examples/ln882h-protocol.yaml
esphome compile examples/rtl8720cf-protocol.yaml
```

Für ein vorhandenes Relais-/Lampen-Gerät das obige Package verwenden: Die vier
Minimalprofile enthalten nur die Diagnoseanwendung und keine gerätespezifische
Steuerung. Der Protokoll-Selbsttest benötigt kein IPv6; LN882H/RTL8720CF können ihn
daher trotz ihrer derzeitigen ESPHome-IPv6-Grenze ausführen.

## Was geprüft wird

- Off, On und Toggle werden mit den originalen CHIP-Buildern serialisiert und
  bytegenau mit festen Referenzvektoren verglichen. Die Original-Parser lesen
  Endpoint 1, OnOff-Cluster `0x0006`, Befehl und Interaction-Model-Revision zurück.
- Jeder der 29 Abschneidepunkte eines Referenzpakets wird geprüft. Jeder zu kleine
  Ausgabepuffer wird einschließlich Schutzbytes vor und hinter dem Puffer geprüft.
- Falscher Wurzeltyp, nachfolgende TLV-Daten und im Test nicht unterstützte zeitlich
  gebundene Aufrufe werden abgewiesen. Weitere Hosttests prüfen fehlende Pfadfelder,
  fehlende Revision und falsche Feldtypen.
- Unabhängige TLV-Vektoren prüfen 64-Bit-Grenzwerte, Byte-Reihenfolge, eine
  Integer-Verengung, eine abgeschnittene Bytefolge und UTF-8-Ausgabe.
- Hosttests laufen mit AddressSanitizer und UndefinedBehaviorSanitizer, einschließlich
  20.000 reproduzierbarer zufälliger Eingaben. Das ist begrenztes Fuzzing und kein
  Nachweis der vollständigen Matter-Konformität.

`protocol_codec.cpp` enthält begrenzte **Test-/Inspektionshilfen** für genau einen
zeitlich ungebundenen OnOff-Aufruf mit leeren Befehlsfeldern und höchstens 512 Bytes.
Diese Hilfen prüfen weder sämtliche Schema-Regeln noch Berechtigungen und dürfen
nicht als authentifizierter Netzwerk-Command-Handler verwendet werden. Der spätere
Server muss CHIP Session Manager, Exchange Manager und Interaction Model Engine
nutzen. Das Experiment implementiert diese Schichten nicht parallel nach.

## Quellen und Wiederverwendung

Der gemeinsame Unterbau liegt in `components/libretiny_matter/vendor/connectedhomeip`.
Er enthält nur die tatsächlich benötigten Upstream-Quellen und Header, einschließlich
der `nLIO`-Byteorder-Abhängigkeit. Beide verwenden Apache-2.0; Lizenzen, Copyrights,
Commit-IDs und Datei-Prüfsummen sind erhalten. Die Testquellen sind eigene Dateien.
Der Speicheradapter verwendet dieselben CHIP-Definitionen.

Der gepinnte CHIP-Entwicklungsstand
`4b4a3e405882afaf8161c0e4af244e65fa8f3f94` kodiert Interaction-Model-Revision 12.
Das ist kein zertifizierter Release dieses Ports. Für einen vollständigen Server
müssen Stack-Version, Konfiguration und Geräteprofile gemeinsam festgelegt werden.
Der Codec-Unterbau darf nicht neben einer zweiten, anders konfigurierten CHIP-
Bibliothek im selben Build eingebunden werden.

```sh
esphome compile tests/protocol-host.yaml
pytest -q tests/test_protocol.py
```

Die nächsten fehlenden Plattformbausteine sind sichere Entropie, Crypto-/Zeit-/Task-
Anbindung und der vollständige Server-Build. Erst danach können Pairing und echte
On/Off-Befehle eines Controllers auf Hardware geprüft werden.
