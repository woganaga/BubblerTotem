#pragma once

void otaInit();

// call every loop() iteration to service pending OTA uploads
void otaHandle();
