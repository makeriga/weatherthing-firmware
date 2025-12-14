#pragma once

#include <Arduino.h>

// Check if factory test has been completed
bool factory_test_completed();

// Run factory test sequence (returns true when test complete)
// Call this in loop() - it handles its own state machine
bool factory_test_run();

// Mark factory test as complete (saves to NVS)
void factory_test_mark_complete();

// Reset factory test flag (for debugging)
void factory_test_reset();
