// SPDX-License-Identifier: MIT

/*
 * Aisthra Arduino-ESP32
 *
 * The engine remains the original C17 translation unit.  This sketch only
 * exercises the public ABI from the Arduino C++ translation unit; it does not
 * add Arduino dependencies to the library itself.
 */

#include <Arduino.h>
#include <aisthra/aisthra.h>

static const int TEMP_PIN = 34;

static ais_ctx_t *ctx = nullptr;
static ais_sensor_id_t sensor = 0u;
static bool ready = false;

void setup()
{
    Serial.begin(115200);

    ais_error_info_t error;
    ais_error_info_init(&error);

    ais_status_t status = ais_ctx_create_default(&ctx, &error);

    if (status == AIS_OK) {
        status = ais_sensor_register_simple(
            ctx,
            AIS_SV("room-temperature"),
            AIS_SV("environment.temperature.v1"),
            AIS_SV("celsius"),
            &sensor, &error);
    }

    if (status != AIS_OK) {
        Serial.println(ais_status_name(status));
        if (ctx != nullptr)
            (void)ais_ctx_close(&ctx, &error);
        return;
    }

    ready = true;
}

void loop()
{
    if (!ready) {
        delay(1000);
        return;
    }

    double millivolts = analogReadMilliVolts(TEMP_PIN);
    double temperature = (millivolts - 500.0) / 10.0;

    ais_ingest_result_t result;
    ais_error_info_t error;
    ais_ingest_result_init(&result);
    ais_error_info_init(&error);

    ais_status_t status = ais_publish_f64(
        ctx, sensor, temperature, &result, &error);

    double latest = 0.0;
    if (status == AIS_OK)
        status = ais_latest_f64(ctx, sensor, &latest, &error);

    if (status == AIS_OK) {
        Serial.printf("Temperature: %.1f C\n", latest);
    } else {
        Serial.println(ais_status_name(status));
    }

    delay(1000);
}
