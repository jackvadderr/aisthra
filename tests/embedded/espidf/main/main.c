/* SPDX-License-Identifier: MIT */

/*
 * ESP-IDF entry point for the existing Aisthra validation program.
 *
 * The canonical implementation remains in examples/; including it here
 * avoids maintaining a second copy of the engine validation logic. The
 * component include path makes the original relative test-header includes
 * resolve without changing the example source.
 */

#include "../../../../examples/esp32-classic-simulation.c"
