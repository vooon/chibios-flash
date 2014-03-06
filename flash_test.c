/*
    ChibiOS/RT - Copyright (C) 2006-2013 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/
/*
 * NOTE: this is standalone app for miniecu board.
 */

#include "ch.h"
#include "hal.h"
#include "flash25.h"
#include "chprintf.h"

static const SPIConfig spi1_cfg = {
	0,
	GPIOA,
	GPIOA_SWDAT_FLASH_CS,
	0, /* hi speed, mode0 */
	0
};

static Flash25Driver FLASH25;
static const Flash25Config flash_cfg = {
	.spip = &SPID1,
	.spicfg = &spi1_cfg
};

static WORKING_AREA(wa_test, 1024);
static msg_t th_test(void *arg __attribute__((unused)))
{
	f25ObjectInit(&FLASH25);
	f25Start(&FLASH25, &flash_cfg);

	while (true) {
		chprintf(&SD1, "Connecting\n");
		blkConnect(&FLASH25);
		chThdSleepMilliseconds(1000);
	};

	return 0;
}

/*
 * Application entry point.
 */
int main(void) {

	/*
	 * System initializations.
	 * - HAL initialization, this also initializes the configured device drivers
	 *   and performs the board-specific initializations.
	 * - Kernel initialization, the main() function becomes a thread and the
	 *   RTOS is active.
	 */
	halInit();
	chSysInit();

	sdStart(&SD1, NULL);

	chThdCreateStatic(wa_test, sizeof(wa_test), NORMALPRIO, th_test, NULL);

	/* we use main thread as idle */
	chThdSetPriority(IDLEPRIO);

	/* This is now the idle thread loop, you may perform here a low priority
	   task but you must never try to sleep or wait in this loop. Note that
	   this tasks runs at the lowest priority level so any instruction added
	   here will be executed after all other tasks have been started.*/
	while (true) {
	}
}
