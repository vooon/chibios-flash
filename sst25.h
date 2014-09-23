/**
 * @file       sst25.h
 * @brief      FLASH25 SST25 driver
 * @author     Vladimir Ermakov Copyright (C) 2014.
 */
/*
 * chibios-flash
 * Copyright (c) 2014, Vlidimir Ermakov, All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 */

#ifndef SST25_H
#define SST25_H

#include "flash-mtd.h"

#define _sst25_driver_data	\
	_base_mtd_driver_data	\
	uint32_t jdec_id;

typedef struct {
	SPIDriver *spip;
	const SPIConfig *spicfg;
} SST25Config;

typedef struct {
	const struct BaseMTDDriverVMT *vmt;
	_sst25_driver_data
	const SST25Config *config;
} SST25Driver;

struct sst25_partition {
	SST25Driver *partp;
	struct mtd_partition definition;
};

#define sst25GetJdecID(flp)	((flp)->jdec_id)

#ifdef __cplusplus
extern "C" {
#endif
	void sst25Init(void);
	void sst25ObjectInit(SST25Driver *flp);
	void sst25Start(SST25Driver *flp, const SST25Config *config);
	void sst25Stop(SST25Driver *flp);
	void sst25InitPartition(SST25Driver *flp, SST25Driver *part_flp, const struct mtd_partition *part_def);
	void sst25InitPartitionTable(SST25Driver *flp, const struct sst25_partition *part_defs);
#ifdef __cplusplus
}
#endif

#endif /* SST25_H */
