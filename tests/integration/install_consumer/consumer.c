/* SPDX-License-Identifier: MIT */

#include <aisthra/aisthra.h>

int main(void)
{
    ais_ctx_t *ctx = NULL;
    ais_sensor_id_t sensor = 0u;
    ais_ingest_result_t result;
    ais_error_info_t error;
    double latest = 0.0;
    ais_error_info_init(&error);
    ais_ingest_result_init(&result);
    if (ais_ctx_create_default(&ctx, &error) != AIS_OK ||
        ais_sensor_register_simple(
            ctx, AIS_SV("installed"),
            AIS_SV("environment.temperature.v1"), AIS_SV("celsius"),
            &sensor, &error) != AIS_OK ||
        ais_publish_f64(ctx, sensor, 21.0, &result, &error) != AIS_OK ||
        ais_latest_f64(ctx, sensor, &latest, &error) != AIS_OK ||
        latest != 21.0) {
        (void)ais_ctx_close(&ctx, &error);
        return 1;
    }
    return ais_ctx_close(&ctx, &error) == AIS_OK && ctx == NULL ? 0 : 1;
}
