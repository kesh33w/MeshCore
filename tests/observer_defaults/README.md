# Observer defaults tests

Run `sh tests/observer_defaults/run.sh` from the repository root. The tests use
the production MQTT initializer, JSON serializer and legacy binary decoder with
the existing host Stream mock. All three builds run with ASan and UBSan.

The builds check Island Link defaults, unchanged ordinary defaults and explicit
compile-time overrides. Saved JSON choices of `none`, an empty preset or IATA,
a custom broker and a different existing preset override the fresh defaults.
An empty preset retains the serializer's existing normalization to `none`.
Round trips check that a later reboot preserves each choice. Legacy binary
migration is also checked before a JSON round trip.

These tests do not emulate CommonCLI filesystem recovery, hardware, Wi-Fi or a
broker connection. `CommonCLI::loadMQTTPrefs` applies the tested initializer
before restoring JSON or binary preferences. Existing native preference-store
tests cover filesystem transaction and recovery behavior.
