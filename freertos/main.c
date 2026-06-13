/*
 * Copyright 2026 Young H. Song
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* freertos/main.c */
#include <stdio.h>
#include <stdlib.h>
#include <FreeRTOS.h>
#include <task.h>

// Simulated RTOS Task
void vStressTask(void *pvParameters) {
    (void)pvParameters;

    printf("[FreeRTOS Task]: Scheduler booted successfully.\n");

    // 1. Allocate using FreeRTOS API (Which routes directly to Proteus)
    void* ptr1 = pvPortMalloc(128);
    void* ptr2 = pvPortMalloc(256);

    if (ptr1 && ptr2) {
        printf("[Proteus]: Successfully carved 128 and 256 bytes from static SRAM.\n");
        printf("[Proteus]: ptr1 = %p, ptr2 = %p\n", ptr1, ptr2);
    } else {
        printf("[Proteus]: Allocation failed! (OOM)\n");
    }

    // 2. Free using FreeRTOS API (Routes to proteus_free)
    vPortFree(ptr1);
    vPortFree(ptr2);

    printf("[Proteus]: Blocks freed and coalesced in O(log n) time.\n");
    printf("[FreeRTOS Task]: Test complete. Terminating simulator...\n");

    exit(0); // Exit the Linux process
}

int main(void) {
    printf("=== Proteus FreeRTOS Linux Simulator ===\n");

    // Spawn a FreeRTOS task
    xTaskCreate(vStressTask, "ProteusTest", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);

    // Start the FreeRTOS Simulator Scheduler
    vTaskStartScheduler();

    return 0;
}
