
#ifndef FLASH25_H
#define FLASH25_H

/* ChibiOS block */
#include "hal.h"

#define _flash25_driver_methods 					\
	_base_block_device_methods 					\
	bool_t (*erase)(void *instance, uint32_t startblk,		\
			uint32_t n);

#define _flash25_driver_data						\
	_base_block_device_data						\
	uint32_t jdec_id;						\
	uint16_t page_size;						\
	uint16_t erase_size;						\
	uint16_t nr_pages;

typedef struct {
	SPIDriver *spip;
	const SPIConfig *spicfg;
} Flash25Config;

struct Flash25DriverVMT {
	_flash25_driver_methods
};

typedef struct {
	const struct Flash25DriverVMT *vmt;
	_flash25_driver_data
	const Flash25Config *config;
} Flash25Driver;

#define f25GetJdecID(flp)	((flp)->jdec_id)
#define f25GetEraseSize(flp)	((flp)->erase_size)
#define f25Erase(flp, sect, n)	((flp)->vmt->erase(flp, sect, n))

#ifdef __cplusplus
extern "C" {
#endif
	void f25Init(void);
	void f25ObjectInit(Flash25Driver *flp);
	void f25Start(Flash25Driver *flp, const Flash25Config *config);
	void f25Stop(Flash25Driver *flp);
#ifdef __cplusplus
}
#endif

#endif /* FLASH25_H */
