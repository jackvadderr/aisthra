/* SPDX-License-Identifier: MIT */

#include <aisthra/advanced.h>

#include <cstring>

int main()
{
    ais_ctx_t *context = nullptr;
    ais_error_info_t error{};
    const char *dynamic_name = "cpp-temperature";
    const ais_string_view_t literal = AIS_SV("environment.temperature.v1");
    const ais_string_view_t dynamic = ais_string_view_make(
        dynamic_name, std::strlen(dynamic_name));
    ais_sensor_id_t sensor = 0;
    ais_ingest_result_t result{};
    double latest = 0.0;
    ais_error_info_init(&error);
    ais_ingest_result_init(&result);
    if (ais_ctx_create_default(&context, &error) != AIS_OK || context == nullptr) {
        return 1;
    }
    if (ais_sensor_register_simple(
            context, dynamic, literal, AIS_SV("celsius"), &sensor,
            &error) != AIS_OK ||
        ais_publish_f64(context, sensor, 20.0, &result, &error) != AIS_OK ||
        ais_latest_f64(context, sensor, &latest, &error) != AIS_OK ||
        latest != 20.0) {
        (void)ais_ctx_close(&context, &error);
        return 1;
    }
    return ais_ctx_close(&context, &error) == AIS_OK && context == nullptr ? 0 : 1;
}
