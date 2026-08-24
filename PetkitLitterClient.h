#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/md5.h>
#include "CreamCatConfig.h"

struct PetkitLitterState {
  bool online = false;
  bool boxFull = false;
  bool sandLack = false;
  bool lowPower = false;
  bool petError = false;
  bool autoWork = false;
  bool manualLock = false;
  int usedTimes = 0;
  int sandPercent = 0;
  int sandWeight = 0;
  int boxState = 0;
  int overall = 0;
  uint32_t updatedAt = 0;
  char errorMsg[40] = "";
};

class PetkitLitterClient {
 public:
  void begin() {
    _configured = configLooksReady();
    if (!_configured) {
      setError("Litter: config empty");
      return;
    }
    WiFi.mode(WIFI_STA);
    updateWifi();
  }

  bool isConfigured() const { return _configured; }
  bool hasStatus() const { return _state.updatedAt > 0; }
  PetkitLitterState state() const { return _state; }
  const char *lastError() const { return _lastError; }

  bool canStartCleaning() const {
    return _state.updatedAt > 0 &&
           _state.online &&
           !_state.boxFull &&
           !_state.sandLack &&
           !_state.lowPower &&
           !_state.petError &&
           !_state.manualLock &&
           strlen(_state.errorMsg) == 0 &&
           _state.boxState <= 1 &&
           _state.overall <= 1;
  }

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
      setError("Litter: WiFi failed");
    }
  }

  bool refreshStatus() {
    if (!readyForCloud()) return false;
    if (!ensureSession()) return false;

    String body = "id=" + urlEncode(CREAMCAT_PETKIT_DEVICE_ID);
    String response;
    if (!petkitPost(deviceEndpoint("device_detail").c_str(), body, response, true)) return false;

    DynamicJsonDocument doc(12000);
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
      setError("Litter: bad JSON");
      return false;
    }

    JsonObject result = doc["result"].as<JsonObject>();
    if (result.isNull()) result = doc.as<JsonObject>();
    JsonObject state = result["state"].as<JsonObject>();
    JsonObject settings = result["settings"].as<JsonObject>();
    if (state.isNull()) {
      setError("Litter: no state");
      return false;
    }

    _state.online = (state["power"] | 0) == 1;
    _state.boxFull = state["boxFull"] | false;
    _state.sandLack = state["sandLack"] | false;
    _state.lowPower = state["lowPower"] | false;
    _state.petError = state["petError"] | false;
    _state.usedTimes = state["usedTimes"] | 0;
    _state.sandPercent = state["sandPercent"] | 0;
    _state.sandWeight = state["sandWeight"] | 0;
    _state.boxState = state["boxState"] | 0;
    _state.overall = state["overall"] | 0;
    _state.autoWork = (settings["autoWork"] | 0) == 1;
    _state.manualLock = (settings["manualLock"] | 0) == 1;

    const char *errorMsg = state["errorMsg"] | "";
    strncpy(_state.errorMsg, errorMsg, sizeof(_state.errorMsg) - 1);
    _state.errorMsg[sizeof(_state.errorMsg) - 1] = '\0';

    _state.updatedAt = millis();
    setError("");
    return true;
  }

  bool startCleaning() {
    if (!readyForCloud()) return false;
    if (!ensureSession()) return false;

    String body = "id=" + urlEncode(CREAMCAT_PETKIT_DEVICE_ID);
    body += "&kv=" + urlEncode("{\"start_action\":0}");
    body += "&type=start";

    String response;
    if (!petkitPost(deviceEndpoint("controlDevice").c_str(), body, response, true)) {
      return false;
    }

    _state.boxState = max(_state.boxState, 2);
    _state.overall = max(_state.overall, 2);
    _state.updatedAt = millis();
    setError("");
    return true;
  }

 private:
  PetkitLitterState _state;
  bool _configured = false;
  uint32_t _lastWifiAttempt = 0;
  uint32_t _sessionExpiresAt = 0;
  char _sessionId[80] = "";
  char _lastError[48] = "Litter: config empty";

  bool configLooksReady() const {
    return CREAMCAT_PETKIT_LITTER_ENABLED &&
           strlen(CREAMCAT_WIFI_SSID) > 0 &&
           strlen(CREAMCAT_WIFI_PASSWORD) > 0 &&
           strlen(CREAMCAT_PETKIT_USERNAME) > 0 &&
           strlen(CREAMCAT_PETKIT_PASSWORD) > 0 &&
           strlen(CREAMCAT_PETKIT_DEVICE_ID) > 0;
  }

  bool readyForCloud() {
    if (!_configured) {
      setError("Litter: config empty");
      return false;
    }
    updateWifi();
    if (WiFi.status() != WL_CONNECTED) {
      setError("Litter: WiFi offline");
      return false;
    }
    return true;
  }

  bool ensureSession() {
    if (strlen(_sessionId) > 0 && millis() < _sessionExpiresAt) return true;
    return login();
  }

  bool login() {
    String passwordMd5 = md5Hex(CREAMCAT_PETKIT_PASSWORD);
    String clientInfo =
        "{'locale': 'en-US', 'name': '23127PN0CG', 'osVersion': '16.1', "
        "'phoneBrand': 'Xiaomi', 'platform': 'android', "
        "'source': 'app.petkit-android', 'version': '13.2.1', "
        "'timezoneId': 'Asia/Shanghai', 'timezone': '8.0'}";

    String body = "oldVersion=13.2.1";
    body += "&client=" + urlEncode(clientInfo);
    body += "&encrypt=1";
    String region = CREAMCAT_PETKIT_REGION;
    region.toLowerCase();
    body += "&region=" + urlEncode(region);
    body += "&username=" + urlEncode(CREAMCAT_PETKIT_USERNAME);
    body += "&password=" + urlEncode(passwordMd5);

    String response;
    if (!petkitPost("user/login", body, response, false)) return false;

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
      setError("Litter: login JSON");
      return false;
    }

    JsonObject session = doc["result"]["session"].as<JsonObject>();
    if (session.isNull()) session = doc["session"].as<JsonObject>();
    const char *sessionId = session["id"] | "";
    int expiresIn = session["expiresIn"] | 3600;
    if (strlen(sessionId) == 0) {
      JsonObject error = doc["error"].as<JsonObject>();
      if (!error.isNull()) {
        setError("Litter: login failed");
      } else {
        setError("Litter: no session");
      }
      return false;
    }

    strncpy(_sessionId, sessionId, sizeof(_sessionId) - 1);
    _sessionId[sizeof(_sessionId) - 1] = '\0';
    uint32_t safeLifetime = max(300, expiresIn - 120);
    _sessionExpiresAt = millis() + safeLifetime * 1000UL;
    setError("");
    return true;
  }

  bool petkitPost(const char *endpoint, const String &body, String &response,
                  bool useSession) {
    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    HTTPClient http;
    String url = apiBaseUrl() + "/" + endpoint;
    if (!http.begin(secureClient, url)) {
      setError("Litter: HTTPS begin");
      return false;
    }
    http.setTimeout(CREAMCAT_PETKIT_HTTP_TIMEOUT_MS);
    addHeaders(http, useSession);

    int status = http.POST(body);
    response = http.getString();
    http.end();

    if (status <= 0) {
      setError("Litter: HTTP failed");
      return false;
    }
    if (status != 200) {
      snprintf(_lastError, sizeof(_lastError), "Litter: HTTP %d", status);
      return false;
    }
    if (response.indexOf("\"error\"") >= 0) {
      setApiError(response);
      return false;
    }
    return true;
  }

  void setApiError(const String &response) {
    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, response)) {
      setError("Litter: API error");
      return;
    }

    JsonObject error = doc["error"].as<JsonObject>();
    if (error.isNull()) {
      setError("Litter: API error");
      return;
    }

    int code = error["code"] | 0;
    const char *msg = error["msg"] | "";
    if (strlen(msg) == 0) {
      msg = error["message"] | "";
    }
    if (strlen(msg) > 0) {
      snprintf(_lastError, sizeof(_lastError), "WC %d: %.36s", code, msg);
    } else {
      snprintf(_lastError, sizeof(_lastError), "WC API code %d", code);
    }
  }

  void addHeaders(HTTPClient &http, bool useSession) {
    http.addHeader("Accept", "*/*");
    http.addHeader("Accept-Language", "en-US;q=1, it-US;q=0.9");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("User-Agent", "okhttp/3.14.9");
    http.addHeader("X-Img-Version", "1");
    http.addHeader("X-Locale", "en-US");
    http.addHeader("X-Client", "android(16.1;23127PN0CG)");
    http.addHeader("X-Hour", "24");
    http.addHeader("X-TimezoneId", "Asia/Shanghai");
    http.addHeader("X-Api-Version", "13.2.1");
    http.addHeader("X-Timezone", "8.0");
    if (useSession && strlen(_sessionId) > 0) {
      http.addHeader("F-Session", _sessionId);
      http.addHeader("X-Session", _sessionId);
    }
  }

  String apiBaseUrl() const {
    String region = CREAMCAT_PETKIT_REGION;
    region.toLowerCase();
    if (region == "cn" || region == "china") return "https://api.petkit.cn/6";
    if (region == "asia") return "https://api.petktasia.com/latest";
    if (region == "eu" || region == "europe") return "https://api.eu-pet.com/latest";
    if (region == "ru" || region == "russia") return "https://api-ru.petkit.cn/latest";
    return "https://api.petkt.com/latest";
  }

  String deviceEndpoint(const char *endpoint) const {
    return String(CREAMCAT_PETKIT_DEVICE_TYPE) + "/" + endpoint;
  }

  void setError(const char *msg) {
    strncpy(_lastError, msg, sizeof(_lastError) - 1);
    _lastError[sizeof(_lastError) - 1] = '\0';
  }

  String md5Hex(const char *value) {
    uint8_t digest[16];
    char hex[33];
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);
    mbedtls_md5_update(&ctx, (const uint8_t *)value, strlen(value));
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);
    for (int i = 0; i < 16; i++) {
      snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    hex[32] = '\0';
    return String(hex);
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
