#pragma once

#include <stdint.h>

namespace ota_updater {

enum class State { Idle, Checking, Available, UpToDate, Downloading, Staged, Failed };

struct Status {
    State state;
    uint32_t written;
    uint32_t total;
    char version[32];
    const char* message;
};

Status status();
bool check();
bool download();
void cancel();
bool activate();
void confirmHealthyBoot();

} // namespace ota_updater
