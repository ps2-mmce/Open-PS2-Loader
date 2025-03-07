
#include <stdint.h>

#include "internal.h"

#include "device.h"
#include "ioplib_util.h"
#include "mmcedrv_config.h"

extern struct cdvdman_settings_mmce cdvdman_settings;

uint32_t (*fp_mmcedrv_get_size)(int fd);
int (*fp_mmcedrv_read_sector)(int type, unsigned int sector_start, unsigned int sector_count, void *buffer);
void (*fp_mmcedrv_config_set)(int setting, int value);
int (*fp_mmcedrv_read)(int fd, int size, void *ptr);
int (*fp_mmcedrv_write)(int fd, int size, void *ptr);
int (*fp_mmcedrv_lseek)(int fd, int offset, int whence);

static int mmce_io_sema;

void DeviceInit(void)
{
    DPRINTF("%s\n", __func__);

    iop_sema_t sema;
    sema.initial = 1;
    sema.max = 1;
    sema.option = 0;
    sema.attr = SA_THPRI;
    mmce_io_sema = CreateSema(&sema);

    DPRINTF("Sema created: 0x%x\n", mmce_io_sema);
}

void DeviceDeinit(void)
{
    DPRINTF("%s\n", __func__);
}

int DeviceReady(void)
{
    // DPRINTF("%s\n", __func__);
    return SCECdComplete;
}

void DeviceFSInit(void)
{
    uint64_t iso_size;

    // get modload export table
    modinfo_t info;
    getModInfo("mmcedrv\0", &info);

    //Get func ptrs
    fp_mmcedrv_get_size = (void *)info.exports[4];
    fp_mmcedrv_read_sector = (void *)info.exports[5];
    fp_mmcedrv_config_set = (void *)info.exports[6];
    fp_mmcedrv_read = (void *)info.exports[7];
    fp_mmcedrv_write = (void *)info.exports[8];
    fp_mmcedrv_lseek = (void *)info.exports[9];

    //Set port and iso fd
    DPRINTF("Port: %i\n", cdvdman_settings.port);
    DPRINTF("Ack wait cycles: %i\n", 0);
    DPRINTF("Use alarms: %i\n", 0);

    fp_mmcedrv_config_set(MMCEDRV_SETTING_PORT, cdvdman_settings.port);
    fp_mmcedrv_config_set(MMCEDRV_SETTING_ACK_WAIT_CYCLES, 0);
    fp_mmcedrv_config_set(MMCEDRV_SETTING_USE_ALARMS, 0);

    DPRINTF("Waiting for device...\n");

    while (1) {
        iso_size = fp_mmcedrv_get_size(cdvdman_settings.iso_fd);
        if (iso_size > 0)
            break;
        DelayThread(100 * 1000); // 100ms
    }

    DPRINTF("Waiting for device...done! connected to %llu byte iso\n", (long long int)iso_size);
}

void DeviceLock(void)
{
    DPRINTF("%s\n", __func__);
    WaitSema(mmce_io_sema);
}

void DeviceUnmount(void)
{
    DPRINTF("%s\n", __func__);
}

void DeviceStop(void)
{
    DPRINTF("%s\n", __func__);
}

int DeviceReadSectors(u64 lsn, void *buffer, unsigned int sectors)
{
    int rv = SCECdErNO;
    int res = 0;
    int retries = 0;

    DPRINTF("%s(%u, 0x%p, %u)\n", __func__, (unsigned int)lsn, buffer, sectors);

    WaitSema(mmce_io_sema);
    do {
        res = fp_mmcedrv_read_sector(cdvdman_settings.iso_fd, (u32)lsn, sectors, buffer);
        retries++;
    } while (res != sectors && retries < 3);
    SignalSema(mmce_io_sema);

    if (retries == 3) {
        DPRINTF("%s: Failed to read after 3 retires, sector: %u, count: %u, buffer: 0x%p\n", __func__, lsn, sectors, buffer);
        rv = SCECdErREAD;
    }

    return rv;
}

//TODO: For VMCs
int mmce_read_offset(int fd, unsigned int offset, unsigned int size, unsigned char *buffer)
{
    DPRINTF("%s\n", __func__);

    WaitSema(mmce_io_sema);
    fp_mmcedrv_lseek(fd, offset, 0);
    fp_mmcedrv_read(fd, size, buffer);
    SignalSema(mmce_io_sema);

    return 1;
}

int mmce_write_offset(int fd, unsigned int offset, unsigned int size, const unsigned char *buffer)
{
    DPRINTF("%s\n", __func__);

    WaitSema(mmce_io_sema);
    fp_mmcedrv_lseek(fd, offset, 0);
    fp_mmcedrv_write(fd, size, buffer);
    SignalSema(mmce_io_sema);

    return 1;
}