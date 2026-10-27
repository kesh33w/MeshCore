#include <assert.h>
#include <stdio.h>
#include <string>

#include "helpers/MQTTDefaults.h"
#include "helpers/MQTTPrefsCodec.h"
#include "helpers/MQTTPrefsSerializer.h"

class InputStream : public Stream {
 public:
  explicit InputStream(const std::string& text) : text_(text) {}
  int available() override { return static_cast<int>(text_.size() - pos_); }
  int read() override { return pos_ < text_.size() ? text_[pos_++] : -1; }
  int peek() override { return pos_ < text_.size() ? text_[pos_] : -1; }
 private:
  std::string text_;
  size_t pos_ = 0;
};

class OutputStream : public Stream {
 public:
  size_t write(uint8_t byte) override { text.push_back(static_cast<char>(byte)); return 1; }
  size_t print(int value, int = DEC) override { return append(value); }
  size_t print(unsigned int value, int = DEC) override { return append(value); }
  size_t print(long value, int = DEC) override { return append(value); }
  size_t print(unsigned long value, int = DEC) override { return append(value); }
  size_t print(long long value, int = DEC) override { return append(value); }
  size_t print(unsigned long long value, int = DEC) override { return append(value); }
  std::string text;
 private:
  template <typename T> size_t append(T value) {
    const std::string number = std::to_string(value);
    text += number;
    return number.size();
  }
};

static MQTTPrefs defaults() {
  MQTTPrefs prefs;
  // Start with nonzero bytes to check that the production initializer clears
  // the whole object, including unused slots and credential buffers.
  memset(&prefs, 0xa5, sizeof(prefs));
  applyMQTTDefaults(&prefs);
  return prefs;
}

static MQTTPrefs restore(const std::string& json) {
  // This is the same default -> parse -> apply order as loadMqttJsonFile().
  // Filesystem recovery and atomic writes have their own native test suites.
  MQTTPrefs prefs = defaults();
  InputStream input(json);
  MQTTPrefsSerializer reader(&prefs);
  assert(reader.loadSerial(input));
  bool repaired = false;
  assert(reader.apply(&repaired));
  return prefs;
}

static void expectRoundTrip(const MQTTPrefs& source) {
  MQTTPrefs saved = source;
  OutputStream output;
  MQTTPrefsSerializer writer(&saved);
  assert(writer.saveSerial(output));
  const MQTTPrefs loaded = restore(output.text);
  assert(memcmp(&source, &loaded, sizeof(source)) == 0);
}

int main() {
  const MQTTPrefs fresh = defaults();
  assert(strcmp(fresh.mqtt_slot_preset[0], EXPECT_SLOT1_PRESET) == 0);
  assert(strcmp(fresh.mqtt_iata, EXPECT_IATA) == 0);
  assert(strcmp(fresh.mqtt_slot_preset[1], EXPECT_SLOT2_PRESET) == 0);
  for (int slot = 2; slot < MQTT_PREFS_SLOT_COUNT; ++slot) {
    assert(strcmp(fresh.mqtt_slot_preset[slot], "none") == 0);
  }
  for (int slot = 0; slot < MQTT_PREFS_SLOT_COUNT; ++slot) {
    assert(fresh.mqtt_slot_packet_filter[slot] == MQTTPacketFilter::kAllPacketTypes);
    assert(fresh.mqtt_slot_host[slot][0] == '\0');
    assert(fresh.mqtt_slot_username[slot][0] == '\0');
    assert(fresh.mqtt_slot_password[slot][0] == '\0');
  }
  assert(fresh.mqtt_packets_enabled == 1 && fresh.mqtt_rx_enabled == 1);
  assert(fresh.mqtt_status_enabled == 1 && fresh.mqtt_tx_enabled == 2);
  expectRoundTrip(fresh);

  const MQTTPrefs missing = restore("{version:1,mqtt:{}}");
  assert(strcmp(missing.mqtt_slot_preset[0], EXPECT_SLOT1_PRESET) == 0);
  assert(strcmp(missing.mqtt_iata, EXPECT_IATA) == 0);

  const MQTTPrefs disabled = restore(
      "{version:1,mqtt:{iata:\"\",slot1:{preset:\"none\"}}}");
  assert(strcmp(disabled.mqtt_slot_preset[0], "none") == 0);
  assert(disabled.mqtt_iata[0] == '\0');
  expectRoundTrip(disabled);

  const MQTTPrefs empty = restore(
      "{version:1,mqtt:{iata:\"\",slot1:{preset:\"\"}}}");
  // Existing normalization canonicalizes an empty preset to disabled "none".
  // It must never replace an explicit empty choice with the new default.
  assert(strcmp(empty.mqtt_slot_preset[0], "none") == 0);
  assert(empty.mqtt_iata[0] == '\0');
  expectRoundTrip(empty);

  const MQTTPrefs custom = restore(
      "{version:1,mqtt:{iata:\"PDX\",slot1:{preset:\"custom\","
      "host:\"broker.example\",port:1883,topic:\"observer/{iata}/{type}\"}}}");
  assert(strcmp(custom.mqtt_slot_preset[0], "custom") == 0);
  assert(strcmp(custom.mqtt_iata, "PDX") == 0);
  assert(strcmp(custom.mqtt_slot_host[0], "broker.example") == 0);
  assert(custom.mqtt_slot_port[0] == 1883);
  assert(strcmp(custom.mqtt_slot_topic[0], "observer/{iata}/{type}") == 0);
  expectRoundTrip(custom);

  const MQTTPrefs previous = restore(
      "{version:1,mqtt:{iata:\"SEA\",slot1:{preset:\"analyzer-us\"},"
      "slot2:{preset:\"none\"}}}");
  assert(strcmp(previous.mqtt_slot_preset[0], "analyzer-us") == 0);
  assert(strcmp(previous.mqtt_slot_preset[1], "none") == 0);
  assert(strcmp(previous.mqtt_iata, "SEA") == 0);
  expectRoundTrip(previous);

  // The update path also supports binary preferences predating /mqtt.json.
  // Use the production encoder/decoder rather than copying fields in the test.
  for (const MQTTPrefs* saved : {&disabled, &empty, &custom, &previous}) {
    LegacyV1MQTTPrefs binary;
    MQTTPrefsCodec::freezeV1(*saved, &binary);
    MQTTPrefs loaded = defaults();
    MQTTPrefsCodec::migrateV1(binary, MQTTPrefsCodec::kV1BaselinePayloadSize, &loaded);
    assert(memcmp(saved, &loaded, sizeof(loaded)) == 0);
    expectRoundTrip(loaded);
  }
  MQTTPrefs binary_empty = disabled;
  binary_empty.mqtt_slot_preset[0][0] = '\0';
  LegacyV1MQTTPrefs binary;
  MQTTPrefsCodec::freezeV1(binary_empty, &binary);
  MQTTPrefs loaded = defaults();
  MQTTPrefsCodec::migrateV1(binary, MQTTPrefsCodec::kV1BaselinePayloadSize, &loaded);
  assert(loaded.mqtt_slot_preset[0][0] == '\0');
  assert(loaded.mqtt_iata[0] == '\0');
  // saveMQTTPrefs normalizes a migrated binary image before writing JSON.
  MQTTPrefsSerializer normalizer(&loaded);
  bool repaired = false;
  assert(normalizer.normalize(&repaired));
  assert(repaired && strcmp(loaded.mqtt_slot_preset[0], "none") == 0);
  assert(loaded.mqtt_iata[0] == '\0');
  expectRoundTrip(loaded);
  printf("PASS: fresh defaults, saved none/empty/custom/existing JSON, reboot round trips, legacy migration (%s / %s)\n",
         EXPECT_SLOT1_PRESET, EXPECT_IATA);
}
