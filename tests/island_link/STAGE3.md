# Stage 3 implementation and evidence

Stage 2 commit: `13048a0a`. Baseline source commit:
`d664d89df66bdacc1319900a2e2d152fa31b9dc9`. Branch: `island-link`.
No flashing, serial access, live relay connection, deployment credential
generation, NAS access, or push was performed.

## Verified in source

The `heltec_v4_repeater_ilink` target extends the unchanged upstream
`heltec_v4_repeater_observer_mqtt` environment and includes a separate
IslandLinkBridge member.
MQTTBridge and the upstream Packet, Dispatcher and Mesh implementations are
unchanged. Only completed `logTx` forwards a packet through `writeTo()`.

The worker owns TLS, identity storage and Wi-Fi fallback. MQTT's existing task
retains Wi-Fi ownership while running. A nonblocking mutex claim prevents an
observer start from overlapping fallback network maintenance. The radio loop
exchanges fixed records through four-entry queues without waiting. It validates
the actual `size_t` length before allocating a mesh packet, marks it seen before
`queueInbound`, and reserves four free packet-pool entries for RF work.

The TLS decoder uses fixed storage for the one-byte length protocol. Zero is a
keepalive. Keepalives run every 30 seconds; receive silence closes a session at
90 seconds. Reconnect delays are 5, 10, 20, 40, 80, 160 and then 300 seconds.
A connection stable for 90 seconds resets the backoff. Disconnects and off/on
changes invalidate queued records. Queue entries and stalled partial writes have
a two-second limit; no outage backlog is replayed.

The receive-origin guard suppresses neighbor/SNR updates for relayed ingress.
`queueInbound` does not call the RF receive logging hook. Subsequent real RF
transmissions remain legitimate TX events for the observer.

## Identity and configuration

The endpoint is `ilink.sacmesh.org:8443`. The sole relay-pin build flag is:

```ini
-D ILINK_RELAY_SPKI_SHA256='"64_HEX_DIGITS_HERE"'
```

It is commented out in the committed build configuration. Missing or malformed
pins prevent every relay connection attempt, and `get ilink` reports
`relay pin not configured`. Identity/storage/resource errors remain visible too.

The pin is SHA-256 of DER SubjectPublicKeyInfo. The TLS policy requires the exact
DNS SAN `ilink.sacmesh.org`, the pinned public key, and proof of possession during
the handshake. It uses no public CA bundle for this connection. The observer's
existing certificate bundle remains available for MQTT.

mbedTLS 2.28.7 rejects VERIFY_REQUIRED with no CA chain even when a custom
verification callback accepts the certificate. Island-link uses VERIFY_OPTIONAL
only to permit a no-CA configuration. Its mandatory pin callback returns a fatal
error that OPTIONAL cannot suppress. The client also requires a peer certificate
and zero verification flags after handshake. Only self-signed trust and date
flags are waived: the pin supplies trust, and first boot has no trusted clock.
Other certificate flags remain failures.

An existing device identity loads immediately. First-boot generation waits for
Wi-Fi association so the ESP32 RF entropy source is active. The P-256 key and
client certificate are committed in one NVS blob, then read back and validated
before use. Corrupt stored identities fail closed instead of silently changing
enrollment. NVS also stores the enabled setting, default off. Enable requests
cannot start TLS before persistence succeeds. `get ilink` shows saving while a
request is pending. The CLI exposes only the public fingerprint and counters.

The offline generator is `scripts/generate_island_link_relay_identity.py`.
It creates a new private directory containing `relay-key.pem` and
`relay-cert.pem`, refuses an existing destination, sets restrictive permissions,
and prints the public pin. It was run only against disposable test directories.
The actual relay key must be generated separately before building a usable image.

Fresh-install defaults are 909.875 MHz, BW 62.5 kHz, SF9, CR5, TX-power setting
10, two-byte locally originated paths, and no automatically configured region.
The earlier report incorrectly attributed a choice of setting 22 after document
review to the user. The intended island setting is 10. The upstream V4 config
notes about 22 dBm actual output at that setting; this is not a measurement.
`MAX_LORA_TX_POWER` remains upstream at 22. Development hardware is a Heltec
V4.3 with the KCT8103L amplifier path, which the existing variant auto-detects.
The home repeater is a Station G3; its power need not match the island node.
No radio-profile lock was added. Existing saved settings remain authoritative
on updates, including path-hash mode, radio settings, admin password and scope.

The public image has an empty initial admin password and refuses all RF logins
until a password is set locally with the existing `password <value>` command.
Empty/overlong passwords are rejected and success does not echo the password.
Automatic setup AP creation is disabled for this USB-first image. The flasher
page and final user instructions remain stage 6 work.

## Tested

After the stage 3 review fix, both environments built successfully with
PlatformIO 6.2.0, espressif32 6.11.0 and Arduino-ESP32 2.0.17:

| Build | Flash bytes | Static RAM bytes |
| --- | ---: | ---: |
| Restored upstream observer | 1,677,133 | 77,912 |
| New ilink environment, pin unconfigured, setting 10 | 1,697,609 | 81,984 |
| Increase over observer | 20,476 | 4,072 |

Removing only the new ilink environment leaves the entire Heltec V4 INI file
byte-identical to the pinned upstream file. The restored observer size matches
the original stage 2 baseline. Neither build produced compiler warnings.
PlatformIO's effective compiler definitions confirm TX power 10, maximum 22,
909.875 MHz, BW 62.5, SF9, CR5 and an empty admin password for ilink.

The original stage 3 build used the observer environment name and setting 22.
It measured 1,697,641 flash bytes and 81,984 static RAM bytes. Those figures are
historical, superseded by the corrected environment above. A synthetic-pin build
also passed during stage 3 without running or connecting it. Current committed
builds leave the relay pin unconfigured.

Actual test results:

- 16,908,315 validator checks passed, covering every header/path encoding at
  lengths 0 through 256. The validator object has no external symbol dependencies.
- 1,000,000 deterministic fuzz inputs passed the acceptance oracle under ASan
  and UBSan: 68,835 accepted, 931,165 rejected.
- Framing, timers, backoff, actual radio admission/transmit, queue limits,
  stale-session rejection and both echo directions passed.
- Actual identity code passed fake-NVS reload, corruption, wrong-type,
  truncation, mismatched key/certificate and entropy-gate tests.
- Real in-memory TLS handshakes passed for an approved client and correctly
  pinned self-signed relay. Wrong pins, wrong SAN with a matching pin,
  unapproved client fingerprints and absent client certificates were refused.
  The server admission checks are a test fixture, not the stage 5 relay.
- The real Mesh/Dispatcher simulation passed flood/PATH/direct/ACK exchange
  for both two-byte and three-byte paths. Its application callbacks are test
  fixtures, not BaseChatMesh. The vendored ed25519 sanitizer exception is
  documented in README.md.
- The relay-key generator passed permissions, SAN, key usage, pin computation,
  and overwrite-refusal tests using temporary credentials.

## Estimated heap headroom

No simultaneous MQTT/island session heap measurement was possible without
hardware. PlatformIO's 3.9% RAM figure uses the board's 2 MiB metadata limit;
it must not be read as available internal heap for TLS.

The installed ESP32-S3 SDK uses internal allocations for mbedTLS and 16 KiB
input and output record buffers. Two TLS sessions therefore need at least
64 KiB of internal heap for record buffers alone. Certificate parsing, keys,
socket state and handshake temporaries add to that. The island worker also has
a 16 KiB internal stack. Its queues and seen table are included in static RAM.

Budget roughly 60 to 80 KiB less internal headroom than the unchanged observer
with the same MQTT connections. This estimate includes the added static RAM,
worker stack and island TLS allocations, with allowance for handshake peaks.
For example, an observer with 120 KiB actually free would be estimated to have
40 to 60 KiB left with island TLS active. The 120 KiB starting value is an
illustrative assumption, not a measurement of this Heltec.

The final linker map leaves 206,704 bytes between `_heap_start` and the end of
its DRAM segment before runtime allocations. That is not a live free-heap
measurement and cannot account for Wi-Fi, RTOS, MQTT clients or fragmentation.
Each additional MQTT TLS session adds at least another 32 KiB of record buffers.
Actual minimum free internal heap, largest free block, and worker stack high-water
mark must be checked with the intended MQTT slot count on hardware.

## Unverified or deferred

ESP32 entropy timing, simultaneous Wi-Fi/MQTT/TLS operation, watchdog stability,
real RF DM delivery, first-install enrollment and enrollment survival across an
actual flash update remain unverified. Saved-setting and identity code plus host
tests do not prove those hardware outcomes. Updates must preserve NVS and the
existing settings partitions; erase-on-update would destroy enrollment.

The home admit operation/interface, reference relay, live allowlist/rate-cap
enforcement and ESP Web Tools page remain stages 4 through 6. Single-byte and
transport-code packets remain excluded exactly as specified. An existing saved
single-byte path mode or scoped traffic will not become eligible merely by
enabling island-link.

## Accepted V4.3 smoke test, 2026-10-26

This follow-up supersedes the earlier hardware and stack estimates above.
Tested: the original 16 KiB IslandLink worker stack overflowed during the first
Wi-Fi-associated key generation, inside `mbedtls_pk_write_key_pem`. Increasing
the worker stack to 32 KiB fixed that observed crash. `get ilink` now reports
`stack_free_min` in bytes, sampled by the worker once per second and published
atomically so the radio loop does not scan stacks.

Tested: key generation left a minimum 16,060 bytes of worker stack free;
identity reload after reboot left 19,548 bytes. The fingerprint survived reboot.
Wi-Fi idle measured 193,368 bytes free internal heap and a largest block of
180,212 bytes. With one sacmesh MQTT session, the last complete sample measured
174,020 bytes free and a largest block of 163,828 bytes. The relay pin remained
unconfigured, so these measurements exclude an active island TLS session.
The fixed build used 1,697,705 flash bytes and 81,992 static RAM bytes; the stack
increase also costs 16 KiB of runtime internal RAM. The existing validator,
framing/timer, radio admission/send and one-million-input fuzz tests passed.

The user accepted Task 1 and approved the stack fix. The brief observation
ended when the user unplugged USB. It is not a completed soak test.

Open for the first live link test, all unverified:

- Worker minimum free stack during the island TLS handshake.
- Free internal heap and largest free block with observer MQTT TLS and island
  TLS both connected, including handshake peaks.
- A full 30-minute soak with reset, crash and watchdog monitoring.
- RF login refusal before the first admin password and successful login after
  it is set over USB.

No live-link gate above is satisfied by the host tests or brief smoke test.
