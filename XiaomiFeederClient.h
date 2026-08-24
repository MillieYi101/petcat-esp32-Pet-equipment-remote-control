  #pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <esp_system.h>
#include <time.h>
#include "CreamCatConfig.h"

struct XiaomiFeederState {
  bool deviceFault = false;
  bool lowFood = false;
  bool jammed = false;
  bool foodOutAbnormal = false;
  bool bowlPiled = false;
  bool dispensing = false;
  int bowlGrams = 0;
  int targetGrams = 0;
  uint32_t updatedAt = 0;
};

class XiaomiFeederClient {
 public:
  void begin() {
    _configured = configLooksReady();
    if (!_configured) {
      setError("Feeder: config empty");
      return;
    }
    WiFi.mode(WIFI_STA);
    updateWifi();
    if (WiFi.status() == WL_CONNECTED) {
      syncClock();
    }
  }

  bool isConfigured() const { return _configured; }
  bool hasStatus() const { return _state.updatedAt > 0; }
  XiaomiFeederState state() const { return _state; }
  const char *lastError() const { return _lastError; }

  void updateWifi() {
    if (!_configured) return;
    if (WiFi.status() == WL_CONNECTED) return;
    if (_lastWifiAttempt != 0 && millis() - _lastWifiAttempt < 10000) return;
    _lastWifiAttempt = millis();

    WiFi.begin(CREAMCAT_WIFI_SSID, CREAMCAT_WIFI_PASSWORD);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 4500) {
      delay(120);
    }
    if (WiFi.status() != WL_CONNECTED) {
      setError("Feeder: WiFi failed");
      return;
    }
    syncClock();
  }

  bool canDispenseNow() const {
    return _state.updatedAt > 0 &&
           !_state.deviceFault &&
           !_state.jammed &&
           !_state.foodOutAbnormal &&
           !_state.bowlPiled &&
           !_state.dispensing;
  }

  bool dispense(uint16_t portions) {
    if (!readyForCloud()) return false;
    portions = constrain(portions, 1, 15);

    // Mi Home exposes this feeder action as portions, not grams.
    String payload = "{\"params\":{\"did\":\"";
    payload += CREAMCAT_XIAOMI_DEVICE_ID;
    payload += "\",\"miid\":0,\"siid\":2,\"aiid\":1,\"in\":[";
    payload += portions;
    payload += "]}}";

    String response;
    if (!miotPost("/v2/miotspec/action", payload, response)) return false;
    if (!responseLooksOk(response)) return false;
    _state.dispensing = true;
    _state.updatedAt = millis();
    setError("");
    return true;
  }

  bool refreshStatus() {
    if (!readyForCloud()) return false;

    String did = CREAMCAT_XIAOMI_DEVICE_ID;
    String payload = "{\"params\":[";
    payload += propJson(did, 2, 1);   // device fault / scale fault
    payload += ",";
    payload += propJson(did, 2, 6);   // pet-food-left-level
    payload += ",";
    payload += propJson(did, 2, 7);   // target feeding measure
    payload += ",";
    payload += propJson(did, 2, 10);  // jam status
    payload += ",";
    payload += propJson(did, 2, 11);  // food-out abnormal status
    payload += ",";
    payload += propJson(did, 2, 15);  // bowl food pile
    payload += ",";
    payload += propJson(did, 2, 22);  // live bowl grams
    payload += ",";
    payload += propJson(did, 2, 26);  // dispensing status
    payload += "]}";

    String response;
    if (!miotPost("/v2/miotspec/prop/get", payload, response)) return false;

    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
      setError("Feeder: bad JSON");
      return false;
    }

    JsonArray result = doc["result"].as<JsonArray>();
    if (result.isNull()) {
      result = doc["data"]["result"].as<JsonArray>();
    }
    if (result.isNull()) {
      setError("Feeder: no status");
      return false;
    }

    for (JsonObject item : result) {
      int siid = item["siid"] | 0;
      int piid = item["piid"] | 0;
      int value = item["value"] | 0;
      if (siid != 2) continue;
      if (piid == 1) _state.deviceFault = value == 1;
      if (piid == 6) _state.lowFood = value == 1;
      if (piid == 7) _state.targetGrams = value;
      if (piid == 10) _state.jammed = value == 1;
      if (piid == 11) _state.foodOutAbnormal = value == 1;
      if (piid == 15) _state.bowlPiled = value == 1;
      if (piid == 22) _state.bowlGrams = value;
      if (piid == 26) _state.dispensing = value == 1;
    }

    _state.updatedAt = millis();
    setError("");
    return true;
  }

 private:
  XiaomiFeederState _state;
  bool _configured = false;
  bool _clockSynced = false;
  uint32_t _lastWifiAttempt = 0;
  char _lastError[48] = "Feeder: config empty";

  bool configLooksReady() const {
    return CREAMCAT_XIAOMI_FEEDER_ENABLED &&
           strlen(CREAMCAT_WIFI_SSID) > 0 &&
           strlen(CREAMCAT_WIFI_PASSWORD) > 0 &&
           strlen(CREAMCAT_XIAOMI_USER_ID) > 0 &&
           strlen(CREAMCAT_XIAOMI_SERVICE_TOKEN) > 0 &&
           strlen(CREAMCAT_XIAOMI_SSECURITY) > 0 &&
           strlen(CREAMCAT_XIAOMI_DEVICE_ID) > 0;
  }

  bool readyForCloud() {
    if (!_configured) {
      setError("Feeder: config empty");
      return false;
    }
    updateWifi();
    if (WiFi.status() != WL_CONNECTED) {
      setError("Feeder: WiFi offline");
      return false;
    }
    if (!_clockSynced) syncClock();
    if (!_clockSynced) return false;
    return true;
  }

  void syncClock() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    uint32_t start = millis();
    while (time(nullptr) < 1600000000 && millis() - start < 4500) {
      delay(120);
    }
    _clockSynced = time(nullptr) >= 1600000000;
    if (!_clockSynced) setError("Feeder: time sync fail");
  }

  void setError(const char *msg) {
    strncpy(_lastError, msg, sizeof(_lastError) - 1);
    _lastError[sizeof(_lastError) - 1] = '\0';
  }

  String apiHost() const {
    if (strlen(CREAMCAT_XIAOMI_API_HOST) > 0) return String(CREAMCAT_XIAOMI_API_HOST);
    String region = CREAMCAT_XIAOMI_REGION;
    region.toLowerCase();
    if (region == "cn") return "api.io.mi.com";
    return region + ".api.io.mi.com";
  }

  String apiUrl(const char *uri) const {
    return "https://" + apiHost() + "/app" + String(uri);
  }

  String propJson(const String &did, int siid, int piid) {
    String item = "{\"did\":\"";
    item += did;
    item += "\",\"miid\":0,\"siid\":";
    item += siid;
    item += ",\"piid\":";
    item += piid;
    item += "}";
    return item;
  }

  bool miotPost(const char *uri, const String &json, String &response) {
    String nonce = makeNonce();
    String signedNonce = makeSignedNonce(CREAMCAT_XIAOMI_SSECURITY, nonce);
    if (nonce.length() == 0 || signedNonce.length() == 0) {
      setError("Feeder: sign failed");
      return false;
    }

    String body;
#if CREAMCAT_XIAOMI_USE_RC4
    body = makeRc4Body(uri, json, nonce, signedNonce);
#else
    body = makePlainBody(uri, json, nonce, signedNonce);
#endif
    if (body.length() == 0) {
      setError("Feeder: body failed");
      return false;
    }

    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    HTTPClient http;
    String url = apiUrl(uri);
    if (!http.begin(secureClient, url)) {
      setError("Feeder: HTTPS begin fail");
      return false;
    }
    http.setTimeout(CREAMCAT_FEEDER_HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("User-Agent", String("Android-7.1.1-1.0.0-ONEPLUS A3010-136-") +
                                     CREAMCAT_XIAOMI_USER_ID +
                                     " APP/com.xiaomi.smarthome");
    http.addHeader("MIOT-REQUEST-MODEL", CREAMCAT_XIAOMI_DEVICE_MODEL);
#if CREAMCAT_XIAOMI_USE_RC4
    http.addHeader("MIOT-ENCRYPT-ALGORITHM", "ENCRYPT-RC4");
#endif
    http.addHeader("Cookie", String("userId=") + CREAMCAT_XIAOMI_USER_ID +
                                 "; serviceToken=" + CREAMCAT_XIAOMI_SERVICE_TOKEN +
                                 "; yetAnotherServiceToken=" + CREAMCAT_XIAOMI_SERVICE_TOKEN);

    int status = http.POST(body);
    String raw = http.getString();
    http.end();

    if (status <= 0) {
      setError("Feeder: HTTP failed");
      return false;
    }
    if (status != 200) {
      snprintf(_lastError, sizeof(_lastError), "Feeder: HTTP %d", status);
      return false;
    }

#if CREAMCAT_XIAOMI_USE_RC4
    response = rc4CryptBase64(signedNonce, raw, true);
    if (response.length() == 0) response = raw;
#else
    response = raw;
#endif
    Serial.println(response);
    return true;
  }

  bool responseLooksOk(const String &response) {
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, response)) {
      setError("Feeder: bad JSON");
      return false;
    }
    int code = doc["code"] | 0;
    if (code != 0) {
      snprintf(_lastError, sizeof(_lastError), "Feeder: code %d", code);
      return false;
    }
    JsonVariant resultCode = doc["result"]["code"];
    if (resultCode.isNull()) resultCode = doc["data"]["code"];
    if (!resultCode.isNull() && resultCode.as<int>() != 0) {
      snprintf(_lastError, sizeof(_lastError), "Feeder: code %d", resultCode.as<int>());
      return false;
    }
    return true;
  }

  String makePlainBody(const char *uri, const String &json, const String &nonce,
                       const String &signedNonce) {
    String signBase = String(uri) + "&" + signedNonce + "&" + nonce + "&data=" + json;
    String signature = hmacSha256Base64(signedNonce, signBase);
    return "_nonce=" + urlEncode(nonce) + "&data=" + urlEncode(json) +
           "&signature=" + urlEncode(signature);
  }

  String makeRc4Body(const char *uri, const String &json, const String &nonce,
                     const String &signedNonce) {
    String rc4Base = String("POST&") + uri + "&data=" + json + "&" + signedNonce;
    String rc4Hash = sha1Base64(rc4Base);
    String encData = rc4CryptBase64(signedNonce, json, false);
    String encRc4Hash = rc4CryptBase64(signedNonce, rc4Hash, false);
    String sigBase = String("POST&") + uri + "&data=" + encData +
                     "&rc4_hash__=" + encRc4Hash + "&" + signedNonce;
    String signature = sha1Base64(sigBase);

    return "data=" + urlEncode(encData) +
           "&rc4_hash__=" + urlEncode(encRc4Hash) +
           "&signature=" + urlEncode(signature) +
           "&ssecurity=" + urlEncode(CREAMCAT_XIAOMI_SSECURITY) +
           "&_nonce=" + urlEncode(nonce);
  }

  String makeNonce() {
    uint8_t bytes[12];
    for (int i = 0; i < 8; i++) bytes[i] = (uint8_t)esp_random();
    uint32_t minutes = (uint32_t)(time(nullptr) / 60);
    bytes[8] = (minutes >> 24) & 0xff;
    bytes[9] = (minutes >> 16) & 0xff;
    bytes[10] = (minutes >> 8) & 0xff;
    bytes[11] = minutes & 0xff;
    return base64Encode(bytes, sizeof(bytes));
  }

  String makeSignedNonce(const char *ssecurity, const String &nonce) {
    uint8_t secret[96];
    uint8_t nonceBytes[24];
    size_t secretLen = 0;
    size_t nonceLen = 0;
    if (!base64Decode(ssecurity, secret, sizeof(secret), &secretLen)) return "";
    if (!base64Decode(nonce, nonceBytes, sizeof(nonceBytes), &nonceLen)) return "";

    mbedtls_sha256_context ctx;
    uint8_t digest[32];
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, secret, secretLen);
    mbedtls_sha256_update(&ctx, nonceBytes, nonceLen);
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    return base64Encode(digest, sizeof(digest));
  }

  String hmacSha256Base64(const String &keyBase64, const String &message) {
    uint8_t key[64];
    size_t keyLen = 0;
    if (!base64Decode(keyBase64, key, sizeof(key), &keyLen)) return "";

    uint8_t digest[32];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(info, key, keyLen, (const uint8_t *)message.c_str(),
                    message.length(), digest);
    return base64Encode(digest, sizeof(digest));
  }

  String sha1Base64(const String &message) {
    uint8_t digest[20];
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, (const uint8_t *)message.c_str(), message.length());
    mbedtls_sha1_finish(&ctx, digest);
    mbedtls_sha1_free(&ctx);
    return base64Encode(digest, sizeof(digest));
  }

  String rc4CryptBase64(const String &keyBase64, const String &payload,
                        bool payloadIsBase64) {
    uint8_t key[64];
    size_t keyLen = 0;
    if (!base64Decode(keyBase64, key, sizeof(key), &keyLen)) return "";

    String inputString = payload;
    uint8_t decoded[2048];
    uint8_t *input = (uint8_t *)inputString.c_str();
    size_t inputLen = inputString.length();
    if (payloadIsBase64) {
      size_t decodedLen = 0;
      if (!base64Decode(payload, decoded, sizeof(decoded), &decodedLen)) return "";
      input = decoded;
      inputLen = decodedLen;
    }

    uint8_t s[256];
    for (int i = 0; i < 256; i++) s[i] = i;
    int j = 0;
    for (int i = 0; i < 256; i++) {
      j = (j + s[i] + key[i % keyLen]) & 0xff;
      uint8_t tmp = s[i];
      s[i] = s[j];
      s[j] = tmp;
    }

    uint8_t discard = 0;
    int i = 0;
    j = 0;
    for (int n = 0; n < 1024; n++) {
      discard = rc4Next(s, i, j);
    }
    (void)discard;

    String output;
    output.reserve(inputLen + 8);
    uint8_t outBytes[2048];
    if (inputLen > sizeof(outBytes)) return "";
    for (size_t n = 0; n < inputLen; n++) {
      outBytes[n] = input[n] ^ rc4Next(s, i, j);
    }

    if (payloadIsBase64) {
      for (size_t n = 0; n < inputLen; n++) output += (char)outBytes[n];
      return output;
    }
    return base64Encode(outBytes, inputLen);
  }

  uint8_t rc4Next(uint8_t s[256], int &i, int &j) {
    i = (i + 1) & 0xff;
    j = (j + s[i]) & 0xff;
    uint8_t tmp = s[i];
    s[i] = s[j];
    s[j] = tmp;
    return s[(s[i] + s[j]) & 0xff];
  }

  String base64Encode(const uint8_t *data, size_t len) {
    size_t outLen = 0;
    mbedtls_base64_encode(NULL, 0, &outLen, data, len);
    uint8_t *out = (uint8_t *)malloc(outLen + 1);
    if (!out) return "";
    if (mbedtls_base64_encode(out, outLen, &outLen, data, len) != 0) {
      free(out);
      return "";
    }
    out[outLen] = '\0';
    String result = (char *)out;
    free(out);
    return result;
  }

  bool base64Decode(const String &input, uint8_t *out, size_t outSize, size_t *outLen) {
    int ret = mbedtls_base64_decode(out, outSize, outLen,
                                    (const uint8_t *)input.c_str(), input.length());
    return ret == 0;
  }

  String urlEncode(const String &value) {
    const char *hex = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(value.length() * 3);
    for (size_t i = 0; i < value.length(); i++) {
      char c = value[i];
      if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
        encoded += c;
      } else {
        encoded += '%';
        encoded += hex[(c >> 4) & 0x0f];
        encoded += hex[c & 0x0f];
      }
    }
    return encoded;
  }
};
