#include "include/opl.h"
#include "include/lang.h"
#include "include/gui.h"
#include "include/supportbase.h"
#include "include/mmcesupport.h"
#include "include/util.h"
#include "include/themes.h"
#include "include/textures.h"
#include "include/ioman.h"
#include "include/system.h"
#include "include/extern_irx.h"
#include "include/cheatman.h"
#include "modules/iopcore/common/cdvd_config.h"
#include "../ee_core/include/coreconfig.h"
#include <usbhdfsd-common.h>

#include <ps2sdkapi.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h> // fileXioIoctl, fileXioDevctl

static char mmcePrefix[40]; // Contains the full path to the folder where all the games are.
static int mmceULSizePrev = -2;
static time_t mmceModifiedCDPrev;
static time_t mmceModifiedDVDPrev;
static int mmceGameCount = 0;
static base_game_info_t *mmceGames;

// forward declaration
static item_list_t mmceGameList;

int mmceDetectSlot(void)
{
    int ret = -1;
    if (fileXioDevctl("mmce0:/", 0x1, NULL, 0, NULL, 0) != -1) {
        sprintf(mmcePrefix, "mmce0:/%s", gMMCEPrefix);
        ret = 2;
    } else if (fileXioDevctl("mmce1:/", 0x1, NULL, 0, NULL, 0) != -1) {
        sprintf(mmcePrefix, "mmce1:/%s", gMMCEPrefix);
        ret = 3;
    }
    return ret;
}

void mmceSetPrefix(void)
{
    if (gMMCESlot == 0)
        sprintf(mmcePrefix, "mmce0:/%s", gMMCEPrefix);
    else if (gMMCESlot == 1)
        sprintf(mmcePrefix, "mmce1:/%s", gMMCEPrefix);
    else if (gMMCESlot == 2)
        (void)mmceDetectSlot();
}

void mmceLoadModules(void)
{
    LOG("MMCESUPPORT LoadModules\n");
    LOG("[MMCEMAN]:\n");
    sysLoadModuleBuffer(&mmceman_irx, size_mmceman_irx, 0, NULL);
}

void mmceInit(item_list_t *itemList)
{
    LOG("MMCESUPPORT Init\n");
    mmceULSizePrev = -2;
    mmceModifiedCDPrev = 0;
    mmceModifiedDVDPrev = 0;
    mmceGameCount = 0;
    mmceGames = NULL;

    configGetInt(configGetByType(CONFIG_OPL), "usb_frames_delay", &mmceGameList.delay);
    mmceGameList.updateDelay = -1; //No automatic updates

    mmceLoadModules();

    if (gMMCESlot == 0)
        sprintf(mmcePrefix, "mmce0:/");
    else if (gMMCESlot == 1)
        sprintf(mmcePrefix, "mmce1:/");
    else if (gMMCESlot == 2)
        mmceDetectSlot();

    mmceGameList.enabled = 1;
}

item_list_t *mmceGetObject(int initOnly)
{
    if (initOnly && !mmceGameList.enabled)
        return NULL;
    return &mmceGameList;
}

static int mmceNeedsUpdate(item_list_t *itemList)
{
    static unsigned char ThemesLoaded = 0;
    static unsigned char LanguagesLoaded = 0;

    char path[256];
    int result = 0;
    struct stat st;

    //Hacky: check if slot was changed, update prefix if needed
    mmceSetPrefix();

    if (mmceULSizePrev == -2)
        result = 1;

    sprintf(path, "%sCD", mmcePrefix);
    if (stat(path, &st) != 0)
        st.st_mtime = 0;

    if (mmceModifiedCDPrev != st.st_mtime) {
        mmceModifiedCDPrev = st.st_mtime;
        result = 1;
    }

    sprintf(path, "%sDVD", mmcePrefix);
    if (stat(path, &st) != 0)
        st.st_mtime = 0;

    if (mmceModifiedDVDPrev != st.st_mtime) {
        mmceModifiedDVDPrev = st.st_mtime;
        result = 1;
    }

    if (!sbIsSameSize(mmcePrefix, mmceULSizePrev))
        result = 1;

    // update Themes
    if (!ThemesLoaded) {
        sprintf(path, "%sTHM", mmcePrefix);
        if (thmAddElements(path, "/", 1) > 0)
            ThemesLoaded = 1;
    }

    // update Languages
    if (!LanguagesLoaded) {
        sprintf(path, "%sLNG", mmcePrefix);
        if (lngAddLanguages(path, "/", mmceGameList.mode) > 0)
            LanguagesLoaded = 1;
    }

    sbCreateFolders(mmcePrefix, 1);

    return result;
}

static int mmceUpdateGameList(item_list_t *itemList)
{
    sbReadList(&mmceGames, mmcePrefix, &mmceULSizePrev, &mmceGameCount);
    return mmceGameCount;
}

static int mmceGetGameCount(item_list_t *itemList)
{
    return mmceGameCount;
}

static void *mmceGetGame(item_list_t *itemList, int id)
{
    return (void *)&mmceGames[id];
}

static char *mmceGetGameName(item_list_t *itemList, int id)
{
    return mmceGames[id].name;
}

static int mmceGetGameNameLength(item_list_t *itemList, int id)
{
    return ((mmceGames[id].format != GAME_FORMAT_USBLD) ? ISO_GAME_NAME_MAX + 1 : UL_GAME_NAME_MAX + 1);
}

static char *mmceGetGameStartup(item_list_t *itemList, int id)
{
    return mmceGames[id].startup;
}

static void mmceDeleteGame(item_list_t *itemList, int id)
{
    sbDelete(&mmceGames, mmcePrefix, "/", mmceGameCount, id);
    mmceULSizePrev = -2;
}

static void mmceRenameGame(item_list_t *itemList, int id, char *newName)
{
    sbRename(&mmceGames, mmcePrefix, "/", mmceGameCount, id, newName);
    mmceULSizePrev = -2;
}

void mmceLaunchGame(item_list_t *itemList, int id, config_set_t *configSet)
{
    int i, index, compatmask = 0;
    int EnablePS2Logo = 0;
    int result;

    char partname[256], filename[32];
    base_game_info_t *game;
    struct cdvdman_settings_mmce *settings;
    u32 layer1_start, layer1_offset;
    unsigned short int layer1_part;

    // No Autolaunch yet
    if (gAutoLaunchBDMGame == NULL)
        game = &mmceGames[id];
    else
        game = gAutoLaunchBDMGame;

    void *irx = &mmce_cdvdman_irx;
    int irx_size = size_mmce_cdvdman_irx;
    compatmask = sbPrepare(game, configSet, irx_size, irx, &index);
    settings = (struct cdvdman_settings_mmce *)((u8 *)irx + index);
    if (settings == NULL)
        return;

    char vmc_name[32];
    char vmc_path[256];
    int vmc_size_mb;
    int vmc_id, size_mcemu_irx = 0;
    int vmc_fd;
    mmce_vmc_infos_t mmce_vmc_infos;
    vmc_superblock_t vmc_superblock;

    for (vmc_id = 0; vmc_id < 2; vmc_id++) {
        memset(&mmce_vmc_infos, 0, sizeof(mmce_vmc_infos));
        configGetVMC(configSet, vmc_name, sizeof(vmc_name), vmc_id);
        if (vmc_name[0]) {
            vmc_size_mb = sysCheckVMC(mmcePrefix, "/", vmc_name, 0, &vmc_superblock);
            if (vmc_size_mb > 0) {
                mmce_vmc_infos.flags = vmc_superblock.mc_flag & 0xFF;
                mmce_vmc_infos.flags |= 0x100;
                mmce_vmc_infos.specs.page_size = vmc_superblock.page_size;
                mmce_vmc_infos.specs.block_size = vmc_superblock.pages_per_block;
                mmce_vmc_infos.specs.card_size = vmc_superblock.pages_per_cluster * vmc_superblock.clusters_per_card;

                sprintf(vmc_path, "%sVMC/%s.bin", mmcePrefix, vmc_name);

                vmc_fd = fileXioOpen(vmc_path, 0x3, 0666);
                if (vmc_fd >= 0) {
                    mmce_vmc_infos.fd = fileXioIoctl2(vmc_fd, 0x80, NULL, 0, NULL, 0);
                    mmce_vmc_infos.active = 1;
                }
            }
        }

        for (i = 0; i < size_mmce_mcemu_irx; i++) {
            if (((u32 *)&mmce_mcemu_irx)[i] == (0xC0DEFAC0 + vmc_id)) {
                if (mmce_vmc_infos.active)
                    size_mcemu_irx = size_mmce_mcemu_irx;
                memcpy(&((u32 *)&mmce_mcemu_irx)[i], &mmce_vmc_infos, sizeof(mmce_vmc_infos_t));
                break;
            }
        }
    }

    // Initialize layer 1 information.
    sbCreatePath(game, partname, mmcePrefix, "/", 0);

    if (gPS2Logo) {
        int fd = open(partname, O_RDONLY, 0666);
        if (fd >= 0) {
            EnablePS2Logo = CheckPS2Logo(fd, 0);
            close(fd);
        }
    }

    layer1_start = sbGetISO9660MaxLBA(partname);

    switch (game->format) {
        case GAME_FORMAT_USBLD:
            layer1_part = layer1_start / 0x80000;
            layer1_offset = layer1_start % 0x80000;
            sbCreatePath(game, partname, mmcePrefix, "/", layer1_part);
            break;
        default: // Raw ISO9660 disc image; one part.
            layer1_part = 0;
            layer1_offset = layer1_start;
    }

    if (sbProbeISO9660(partname, game, layer1_offset) != 0) {
        layer1_start = 0;
        LOG("DVD detected.\n");
    } else {
        layer1_start -= 16;
        LOG("DVD-DL layer 1 @ part %u sector 0x%lx.\n", layer1_part, layer1_offset);
    }
    settings->common.layer1_start = layer1_start;

    if ((result = sbLoadCheats(mmcePrefix, game->startup)) < 0) {
        if (gAutoLaunchBDMGame == NULL) {
            switch (result) {
                case -ENOENT:
                    guiWarning(_l(_STR_NO_CHEATS_FOUND), 10);
                    break;
                default:
                    guiWarning(_l(_STR_ERR_CHEATS_LOAD_FAILED), 10);
            }
        } else
            LOG("Cheats error\n");
    }

    if (gRememberLastPlayed) {
        configSetStr(configGetByType(CONFIG_LAST), "last_played", game->startup);
        saveConfig(CONFIG_LAST, 0);
    }

    if (configGetStrCopy(configSet, CONFIG_ITEM_ALTSTARTUP, filename, sizeof(filename)) == 0)
        strcpy(filename, game->startup);


    //MMCEDRV settings
    if (gMMCESlot == 0)
        settings->port = 2;
    else if (gMMCESlot == 1)
        settings->port = 3;
    else if (gMMCESlot == 2)
        settings->port = mmceDetectSlot();

    int iso_file = fileXioOpen(partname, 0x1, 0666);
    if (iso_file < 0) {
        LOG("Failed to open iso, aborting\n");
        return;
    }

    settings->ack_wait_cycles = gMMCEAckWaitCycles;
    settings->use_alarms = gMMCEUseAlarms;

    //TEMP: The fd given by sd2psx is not the same one we see here on the EE
    //and ps2sdk_get_iop_fd does not seem to return the right value either
    settings->iso_fd = fileXioIoctl2(iso_file, 0x80, NULL, 0, NULL, 0);

    LOG("name: %s\n", game->name);
    LOG("start: %s\n", game->startup);

    //Set gameid and poll card until ready
#ifdef __DEBUG
    if (gMMCEEnableGameID) {
#endif

    // Send GameID to MMCE
    fileXioDevctl(mmcePrefix, 0x8, game->startup, (strlen(game->startup) + 1), NULL, 0);

    for (int i = 0; i < 15; i++) {
        sleep(1);

        // Poll MMCE status until busy bit is clear
        if ((fileXioDevctl(mmcePrefix, 0x2, NULL, 0, NULL, 0) & 1) == 0) {
            LOG("Set MMCE GameID to: %s\n", game->startup);
            break;
        }
    }
#ifdef __DEBUG
    }
#endif

    //mcReset();
    //mcInit(MC_TYPE_XMC);

    if (gAutoLaunchBDMGame == NULL)
        deinit(NO_EXCEPTION, MMCE_MODE); // CAREFUL: deinit will call mmceCleanUp, so mmceGames/game will be freed

    /* No autolaunch yet
    else {
        miniDeinit(configSet);

        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;
    }*/

    settings->common.zso_cache = 0;

    sysLaunchLoaderElf(filename, "MMCE_MODE", irx_size, irx, size_mcemu_irx, mmce_mcemu_irx, EnablePS2Logo, compatmask);
}

static config_set_t *mmceGetConfig(item_list_t *itemList, int id)
{
    return sbPopulateConfig(&mmceGames[id], mmcePrefix, "/");
}

static int mmceGetImage(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    char path[256];
    if (isRelative)
        snprintf(path, sizeof(path), "%s%s/%s_%s", mmcePrefix, folder, value, suffix);
    else
        snprintf(path, sizeof(path), "%s%s_%s", folder, value, suffix);
    return texDiscoverLoad(resultTex, path, -1);
}

static int mmceGetTextId(item_list_t *itemList)
{
    int mode = _STR_MMCE_GAMES;

    return mode;
}

static int mmceGetIconId(item_list_t *itemList)
{
    //Reusing BDM icon for now
    int mode = MMCE_ICON;

    return mode;
}

// This may be called, even if mmceInit() was not.
static void mmceCleanUp(item_list_t *itemList, int exception)
{
    if (mmceGameList.enabled) {
        LOG("MMCESUPPORT CleanUp\n");

        free(mmceGames);

        //      if ((exception & UNMOUNT_EXCEPTION) == 0)
        //          ...
    }
}

// This may be called, even if mmceInit() was not.
static void mmceShutdown(item_list_t *itemList)
{
    if (mmceGameList.enabled) {
        LOG("MMCESUPPORT Shutdown\n");

        free(mmceGames);
    }

    // As required by some (typically 2.5") HDDs, issue the SCSI STOP UNIT command to avoid causing an emergency park.
    //fileXioDevctl("mass:", USBMASS_DEVCTL_STOP_ALL, NULL, 0, NULL, 0);
}

static int mmceCheckVMC(item_list_t *itemList, char *name, int createSize)
{
    return sysCheckVMC(mmcePrefix, "/", name, createSize, NULL);
}

static char *mmceGetPrefix(item_list_t *itemList)
{
    return mmcePrefix;
}

static item_list_t mmceGameList = {
    MMCE_MODE, 2, 0, 0, MENU_MIN_INACTIVE_FRAMES, MMCE_MODE_UPDATE_DELAY, NULL, NULL, &mmceGetTextId, &mmceGetPrefix, &mmceInit, &mmceNeedsUpdate,
    &mmceUpdateGameList, &mmceGetGameCount, &mmceGetGame, &mmceGetGameName, &mmceGetGameNameLength, &mmceGetGameStartup, &mmceDeleteGame, &mmceRenameGame,
    &mmceLaunchGame, &mmceGetConfig, &mmceGetImage, &mmceCleanUp, &mmceShutdown, &mmceCheckVMC, &mmceGetIconId};

void mmceInitSemaphore()
{
    // Create a semaphore so only one thread can load IOP modules at a time.
    //if (mmceLoadModuleLock < 0) {
    //    mmceLoadModuleLock = sbCreateSemaphore();
    //}
}
