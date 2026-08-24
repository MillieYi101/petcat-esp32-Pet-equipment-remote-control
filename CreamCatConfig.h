#pragma once

// Public, safe defaults. If you fill real Xiaomi tokens, prefer putting them in
// CreamCatSecrets.h instead; that file is ignored by git.
#if __has_include("CreamCatSecrets.h")
#include "CreamCatSecrets.h"
#endif

#ifndef CREAMCAT_WIFI_SSID
#define CREAMCAT_WIFI_SSID ""
#endif

#ifndef CREAMCAT_WIFI_PASSWORD
#define CREAMCAT_WIFI_PASSWORD ""
#endif

// Keep this 0 until the Xiaomi values below are ready.
#ifndef CREAMCAT_XIAOMI_FEEDER_ENABLED
#define CREAMCAT_XIAOMI_FEEDER_ENABLED 0
#endif

// Xiaomi region examples: "cn", "de", "sg", "us", "ru", "in".
// Use the same region that your Mi Home app uses.
#ifndef CREAMCAT_XIAOMI_REGION
#define CREAMCAT_XIAOMI_REGION "cn"
#endif

#ifndef CREAMCAT_XIAOMI_USER_ID
#define CREAMCAT_XIAOMI_USER_ID ""
#endif

#ifndef CREAMCAT_XIAOMI_SERVICE_TOKEN
#define CREAMCAT_XIAOMI_SERVICE_TOKEN ""
#endif

#ifndef CREAMCAT_XIAOMI_SSECURITY
#define CREAMCAT_XIAOMI_SSECURITY ""
#endif

#ifndef CREAMCAT_XIAOMI_DEVICE_ID
#define CREAMCAT_XIAOMI_DEVICE_ID ""
#endif

#ifndef CREAMCAT_XIAOMI_DEVICE_MODEL
#define CREAMCAT_XIAOMI_DEVICE_MODEL "xiaomi.feeder.pi2001"
#endif

// Optional override. Leave empty to derive the host from the region.
#ifndef CREAMCAT_XIAOMI_API_HOST
#define CREAMCAT_XIAOMI_API_HOST ""
#endif

#ifndef CREAMCAT_FEEDER_DEFAULT_PORTIONS
#define CREAMCAT_FEEDER_DEFAULT_PORTIONS 1
#endif

#ifndef CREAMCAT_FEEDER_REFRESH_MS
#define CREAMCAT_FEEDER_REFRESH_MS 30000
#endif

#ifndef CREAMCAT_FEEDER_HTTP_TIMEOUT_MS
#define CREAMCAT_FEEDER_HTTP_TIMEOUT_MS 8000
#endif

// Xiaomi cloud commonly expects RC4-encrypted MIoT payloads.
#ifndef CREAMCAT_XIAOMI_USE_RC4
#define CREAMCAT_XIAOMI_USE_RC4 1
#endif

// PETKIT / 小佩 litter box integration is staged as read-only first.
#ifndef CREAMCAT_PETKIT_LITTER_ENABLED
#define CREAMCAT_PETKIT_LITTER_ENABLED 0
#endif

#ifndef CREAMCAT_PETKIT_USERNAME
#define CREAMCAT_PETKIT_USERNAME ""
#endif

#ifndef CREAMCAT_PETKIT_PASSWORD
#define CREAMCAT_PETKIT_PASSWORD ""
#endif

#ifndef CREAMCAT_PETKIT_REGION
#define CREAMCAT_PETKIT_REGION "CN"
#endif

#ifndef CREAMCAT_PETKIT_DEVICE_ID
#define CREAMCAT_PETKIT_DEVICE_ID ""
#endif

#ifndef CREAMCAT_PETKIT_DEVICE_TYPE
#define CREAMCAT_PETKIT_DEVICE_TYPE "t4"
#endif

#ifndef CREAMCAT_PETKIT_REFRESH_MS
#define CREAMCAT_PETKIT_REFRESH_MS 60000
#endif

#ifndef CREAMCAT_PETKIT_HTTP_TIMEOUT_MS
#define CREAMCAT_PETKIT_HTTP_TIMEOUT_MS 9000
#endif
