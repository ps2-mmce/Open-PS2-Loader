#ifndef __MMCE_SUPPORT_H
#define __MMCE_SUPPORT_H

#include "include/iosupport.h"
#include "include/mcemu.h"

#define MMCE_MODE_UPDATE_DELAY MENU_UPD_DELAY_GENREFRESH

typedef struct
{
    int active;       /* Activation flag */
    int fd;           /* VMC fd */
    int flags;        /* Card flag */
    vmc_spec_t specs; /* Card specifications */
} mmce_vmc_infos_t;

void mmceInit(item_list_t *itemList);
item_list_t *mmceGetObject(int initOnly);
void mmceLoadModules(void);
void mmceLaunchGame(item_list_t *itemList, int fd, config_set_t *configSet);

#endif