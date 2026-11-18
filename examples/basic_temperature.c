// SPDX-License-Identifier: MIT

#include <aisthra/aisthra.h>
#include <stdio.h>

int main(void)
{
    ais_ctx_t *ctx = NULL;
    ais_sensor_id_t sensor = 0u;
    ais_ingest_result_t result;
    ais_error_info_t error;
    double value = 0.0;
    ais_status_t status;

    // Initialize the result and error structures.
    ais_error_info_init(&error);
    ais_ingest_result_init(&result);

    // Create an Aisthra context with default settings.
    status = ais_ctx_create_default(&ctx, &error);

    // Register a temperature sensor using the Celsius unit.
    if (status == AIS_OK)
        status = ais_sensor_register_simple(ctx, AIS_SV("temperature-0"),
            AIS_SV("environment.temperature.v1"), AIS_SV("celsius"),
            &sensor, &error);

    // Publish a temperature reading of 23.5 degrees.
    if (status == AIS_OK)
        status = ais_publish_f64(ctx, sensor, 23.5, &result, &error);

    // Retrieve the latest temperature reading.
    if (status == AIS_OK)
        status = ais_latest_f64(ctx, sensor, &value, &error);

    // Report errors and release resources if an operation fails.
    if (status != AIS_OK) {
        fprintf(stderr, "operation failed: %s\n", ais_status_name(status));
        (void)ais_ctx_close(&ctx, &error);
        return 1;
    }

    // Display the temperature and its observation sequence.
    printf("temperature=%.1f C (sequence=%llu)\n", value,
           (unsigned long long)result.sequence);

    // Release the context and its managed resources.
    (void)ais_ctx_close(&ctx, &error);
    return ctx == NULL ? 0 : 1;
}
