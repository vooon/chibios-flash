
// TODO: add "based on 25xx.c from ChibiOS-EEPROM"
// TODO: check support for non sst25 devices

#include "flash25.h"

/* SST25 command set */
#define CMD_READ		0x03
#define CMD_FREAD		0x0b
#define CMD_ERASE_4K		0x20
#define CMD_ERASE_32K		0x52
#define CMD_ERASE_64K		0xd8
#define CMD_CHIP_ERASE		0x60 /* or 0xc7 */
#define CMD_BYTE_PROG		0x02
#define CMD_AAI_WORD_PROG	0xad
#define CMD_RDSR		0x05
#define CMD_EWSR		0x50
#define CMD_WRSR		0x01
#define CMD_WREN		0x06
#define CMD_WRDI		0x04
#define CMD_RDID		0x90
#define CMD_JDEC_ID		0x9f
#define CMD_EBSY		0x70
#define CMD_DBSY		0x80

/* SST25 status register bits */
#define STAT_BUSY		(1<<0)
#define STAT_WEL		(1<<1)
#define STAT_BP0		(1<<2)
#define STAT_BP1		(1<<3)
#define STAT_BP2		(1<<4)
#define STAT_BP3		(1<<5)
#define STAT_AAI		(1<<6)
#define STAT_BPL		(1<<7)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr)	(sizeof(arr)/sizeof(arr[0]))
#endif

#define INFO(name_, id_, ps_, es_, nr_)		{ /*name_,*/ id_, ps_, es_, nr_ }
struct flash_info {
	/*const char *name;*/
	uint32_t jdec_id;
	uint16_t page_size;
	uint16_t erase_size;
	uint16_t nr_pages;
};

static const struct flash_info flash_info_table[] = {
	INFO("sst25vf016b", 0xbf2541, 256, 4096, 16*1024*1024/8/256),
	INFO("sst25vf032b", 0xbf254a, 256, 4096, 32*1024*1024/8/256)
};


static void flash_transfer(const Flash25Config *cfg,
		const uint8_t *txbuf, size_t txlen,
		uint8_t *rxbuf, size_t rxlen)
{
#if SPI_USE_MUTUAL_EXCLUSION
	spiAcquireBus(cfg->spip);
#endif

	spiStart(cfg->spip, cfg->spicfg);
	spiSelect(cfg->spip);
	spiSend(cfg->spip, txlen, txbuf);
	if (rxlen)
		spiReceive(cfg->spip, rxlen, rxbuf);
	spiUnselect(cfg->spip);

#if SPI_USE_MUTUAL_EXCLUSION
	spiReleaseBus(cfg->spip);
#endif
}

static bool_t flash_is_busy(const Flash25Config *cfg)
{
	uint8_t cmd = CMD_RDSR;
	uint8_t stat;

	flash_transfer(cfg, &cmd, 1, &stat, 1);
	return !!(stat & STAT_BUSY);
}

static uint32_t flash_get_jdec_id(const Flash25Config *cfg)
{
	uint8_t cmd = CMD_JDEC_ID;
	uint8_t jdec[5];

	/* JDEC: 3 bytes + 2 bytes extended id */
	flash_transfer(cfg, &cmd, 1, jdec, sizeof(jdec));
	return (jdec[0] << 16) | (jdec[1] << 8) | jdec[2];
}

/*
 * VMT functions
 */

static bool_t f25_vmt_nop(void *instance __attribute__((unused)))
{
	return CH_SUCCESS;
}

static bool_t f25_connect(Flash25Driver *inst)
{
	struct flash_info *ptbl;

	inst->state = BLK_CONNECTING;
	inst->jdec_id = flash_get_jdec_id(inst->config);

	for (ptbl = flash_info_table;
			ptbl < (flash_info_table + ARRAY_SIZE(flash_info_table));
			ptbl++)
		if (ptbl->jdec_id == inst->jdec_id) {
			inst->state = BLK_ACTIVE;
			inst->page_size = ptbl->page_size;
			inst->erase_size = ptbl->erase_size;
			inst->nr_pages = ptbl->nr_pages;
			return CH_SUCCESS;
		}

	inst->state = BLK_STOP;
	return CH_FAILED;
}

static bool_t f25_read(Flash25Driver *inst, uint32_t startblk,
		uint8_t *buffer, uint32_t n)
{

	return CH_SUCCESS;
}

static bool_t f25_write(Flash25Driver *inst, uint32_t startblk,
		const uint8_t *buffer, uint32_t n)
{
	return CH_SUCCESS;
}

static bool_t f25_erase(Flash25Driver *inst, uint32_t startsect, uint32_t n)
{
	return CH_SUCCESS;
}

static bool_t f25_get_info(Flash25Driver *inst, BlockDeviceInfo *bdip)
{
	if (inst->state != BLK_ACTIVE)
		return CH_FAILED;

	bdip->blk_size = inst->page_size;
	bdip->blk_num = inst->nr_pages;
	return CH_SUCCESS;
}

static const struct Flash25DriverVMT f25_vmt = {
	.is_inserted = f25_vmt_nop,
	.is_protected = f25_vmt_nop,
	.connect = (bool_t (*)(void*)) f25_connect,
	.disconnect = f25_vmt_nop,
	.read = (bool_t (*)(void*, uint32_t, uint8_t*, uint32_t)) f25_read,
	.write = (bool_t (*)(void*, uint32_t, const uint8_t*, uint32_t)) f25_write,
	.sync = f25_vmt_nop,
	.get_info = (bool_t (*)(void*, BlockDeviceInfo*)) f25_get_info,
	.erase = (bool_t (*)(void*, uint32_t, uint32_t)) f25_erase
};

/*
 * public interface
 */

/**
 * @brief Flash25 driver initialization.
 *
 * @init
 */
void f25Init(void)
{
}

/**
 * @brief Initializes an instance.
 *
 * @init
 */
void f25ObjectInit(Flash25Driver *flp)
{
	chDbgCheck(flp != NULL, "f25ObjectInit");

	flp->vmt = &f25_vmt;
	flp->config = NULL;
	flp->state = BLK_STOP;
	flp->jdec_id = 0;
	flp->page_size = 0;
	flp->erase_size = 0;
	flp->nr_pages = 0;
}

void f25Start(Flash25Driver *flp, const Flash25Config *cfg)
{
	chDbgCheck((flp != NULL) && (cfg != NULL), "f25Start");
	chDbgAssert((flp->state == BLK_STOP) || (flp->state == BLK_ACTIVE),
			"f25Start()", "invalid state");

	flp->config = cfg;
	flp->state = BLK_ACTIVE;
}

void f25Stop(Flash25Driver *flp)
{
	chDbgCheck(flp != NULL, "f25Stop");
	chDbgAssert((flp->state == BLK_STOP) || (flp->state == BLK_ACTIVE),
			"f25Start()", "invalid state");

	spiStop(flp->config->spip);
	flp->state = BLK_STOP;
}

