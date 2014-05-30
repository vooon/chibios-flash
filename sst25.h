
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
