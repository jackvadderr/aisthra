/* SPDX-License-Identifier: MIT */

#include <aisthra/aisthra.h>
#include <aisthra/observation.h>
#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char text[128];
    ais_ctx_t *ctx = NULL;
    ais_observation_view_t observation;
    ais_error_info_t error;
    size_t text_size = size < sizeof(text) ? size : sizeof(text);
    for (size_t index = 0u; index < text_size; ++index)
        text[index] = (char)data[index];
    ais_error_info_init(&error);
    if (ais_ctx_create_default(&ctx, &error) != AIS_OK)
        return 0;
    ais_observation_view_init(&observation);
    observation.observation_id = ais_string_view_make(text, text_size);
    observation.session_id = ais_string_view_make(text, text_size);
    observation.observation_type = ais_string_view_make(text, text_size);
    observation.unit_id = ais_string_view_make(text, text_size);
    observation.reference_frame_id = ais_string_view_make(text, text_size);
    observation.provenance.source_type = ais_string_view_make(text, text_size);
    observation.provenance.provider_version = ais_string_view_make(text, text_size);
    observation.provenance.rule_or_model = ais_string_view_make(text, text_size);
    observation.provenance.source_observation_id = ais_string_view_make(text, text_size);
    (void)ais_observation_validate(ctx, &observation, &error);
    (void)ais_ctx_close(&ctx, &error);
    return 0;
}
