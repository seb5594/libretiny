# Native Matter storage adapter

`libretiny_matter_storage` implements the original
[`chip::PersistentStorageDelegate`](https://github.com/project-chip/connectedhomeip/blob/4b4a3e405882afaf8161c0e4af244e65fa8f3f94/src/lib/core/CHIPPersistentStorageDelegate.h)
using actual CHIP error types and FlashDB. The [native server](matter-server.md)
uses it for fabric certificates, keys and cluster state.

## Integration

```yaml
external_components:
  - source:
      type: local
      path: /path/to/libretiny/experiments/esphome-matter/components
    components: [libretiny_matter, libretiny_matter_storage]

libretiny_matter_storage:
  id: matter_storage
```

`examples/bk7231n-storage.yaml` and `examples/bk7238-t1-storage.yaml` build the same
adapter. Tested versions are ESPHome 2026.10.0 and official LibreTiny 1.14.0.
Integration uses the currently public `LibreTinyPreferences::db`; changes to that
interface require adaptation. Other chip families are not enabled yet.

`id(matter_storage).get_delegate()` returns a `chip::PersistentStorageDelegate *`
to ESPHome lambdas. The rs-matter worker uses it through the main-loop dispatcher.
Setup only binds to the existing initialized database; it writes no test values
and does not initialize or format a separate partition.

## Storage contract

| Case | Behavior |
| --- | --- |
| Keys | 1–32 bytes, unchanged after `matter/`; no hashing collisions |
| Values | Binary data up to 2,048 bytes, including empty values |
| Get with a small buffer | Copies the value's prefix, returns copied size and `CHIP_ERROR_BUFFER_TOO_SMALL` |
| Get with `nullptr`, size 0 | Existence check; an existing empty value succeeds |
| Missing key | `CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND`, including deletion |
| Invalid parameters | `CHIP_ERROR_INVALID_ARGUMENT`; no database mutation |
| Full FlashDB | `CHIP_ERROR_NO_MEMORY`; no false write success |
| Reported flash write/read failure | `CHIP_ERROR_PERSISTED_STORAGE_FAILED` |
| Call outside the owning task | `CHIP_ERROR_INCORRECT_STATE` before database access |
| ESPHome factory reset/deinitialized DB | Further accesses rejected; shared partition means shared reset |

The prefix separates Matter keys from ESPHome's numeric preference keys. The 2-KiB
limit leaves room for keys and FlashDB headers within 4-KiB sectors. It is a limit
of this backend, not a claim about all possible Matter storage requirements.
ESPHome and Matter share the existing partition capacity. The adapter adds neither
its own heap buffer nor a deferred write queue.

## Tasks, errors and lifecycle

ESPHome owns the database. All adapter operations must run in the main task captured
at setup; this is checked at runtime. Direct worker calls are rejected. The server
dispatches requests through a one-entry queue and waits for acknowledgement. A
Matter-only mutex would not protect concurrent accesses from ESPHome preferences.

After an ESPHome factory reset, the binding belongs to the old lifecycle; it is
recreated on boot. `FlashDBStorage` cannot be rebound to a different database.
Database and context must outlive the delegate. Interrupt calls are unsupported.
The server's **Matter-only reset** removes Matter state through upstream APIs and
retains ESPHome preferences; it does not deinitialize the shared database.

FlashDB 1.2.0 does not propagate all metadata lookup failures. A CRC-discarded entry
can look like a missing key. The adapter checks returned CRC status and blob reads,
but cannot recover error distinctions that the backend does not expose. Reliable
product use still needs an audit of these paths and physical power-loss tests.
FlashDB provides neither encryption at rest nor a transaction across multiple keys.
An interrupted or failed Matter reset may therefore leave partially cleared state.

## Reused code

- Unmodified CHIP headers from `project-chip/connectedhomeip`, commit
  `4b4a3e405882afaf8161c0e4af244e65fa8f3f94`, Apache-2.0. Delegate/error types and
  [codec tests](protocol-tests.md) share `components/libretiny_matter/vendor/connectedhomeip`.
- `upstream.json` records origins and SHA-256 hashes. Tests verify them; upstream
  formatting and copyright notices are preserved.
- LibreTiny FlashDB 1.2.0, commit `d5c892f0c545884e9f0de0cf67215e426dc8109d`.
  Firmware uses the existing framework package. `tools/fetch_flashdb.py` downloads
  the same revision for host tests; firmware does not link a second FlashDB copy.

C++ sources are a local PlatformIO library. The server uses rs-matter behind a
small C ABI rather than linking another C++ CHIP version. Their different roles
are documented in [architecture.md](architecture.md).

## Tests

Host tests compile the production adapter, original CHIP headers and FlashDB C
sources. Only the FAL hardware boundary is replaced by a 32-KiB NOR simulator:
erase sets bytes to `0xff`; programming permits only 1-to-0 transitions. A second
process reloads the flash image to check persistence without reusing RAM caches.
Other cases cover the storage contract, failed writes/blob reads, full storage and
reclamation, namespace isolation and wrong-task access.

```sh
python tools/fetch_flashdb.py
pytest -q tests/test_storage.py
```

The independent server suite uses the same database and simulator for actual
pairing, fabric persistence, CASE after restart, rejected commissioning after a
fabric write failure, Matter-only reset and reset deletion failure. A numeric
ESPHome preference sentinel verifies that Matter reset leaves unrelated state.
These tests do not simulate power loss during a real flash operation.
