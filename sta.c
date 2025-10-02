#define __USE_MINGW_ANSI_STDIO 0
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define TITLE "sta"
#define ADDITIONAL_HELP "[-y] Suppress warnings about files not being Android boot images\r\n" \
    "[-p <FILEPATH>] Specify a custom path to the boot image\r\n" \
    "[-a] Flash slot A\r\n" \
    "[-b] Flash slot B\r\n" \
    "[-a -b] Flash both slot A and B"
#include "sta2.h"

/* https://github.com/ArrowOS-Devices/android_device_xiaomi_nabu/blob/arrow-13.1/gpt-utils/gpt-utils.h */
#define AB_FLAG_OFFSET 6
#define AB_PARTITION_ATTR_SLOT_ACTIVE 0x1<<2

const char *BOOT = "boot";
const char *BOOT_A = "boot_a";
const char *BOOT_B = "boot_b";
const char *BOOT_PATH = "C:\\boot.img";

int main(int argc, char *argv[])
{
    int staOptions = INVOKE_EXIT;
    char partitionPath[28];
    const char *filePath = BOOT_PATH;
    const char *partitionName = BOOT;
    char *buffer;
    HANDLE hInput;
    LARGE_INTEGER fileSize;
    ULONGLONG flashSize;

    partitionList *partitionLists = NULL;
    unsigned diskAmount = 0;

    LOG("preargparse: arguments count %i\n", argc-1);
    /* parse the arguments */
    {
        int i;
        for (i = 1; i < argc; i++) {
            /* make sure the supplied value is a valid argument */
            if (argv[i][0] != '-' || argv[i][1] == '\0' || argv[i][2] != '\0') { /* stop on args that do not start with a dash (-), are one character long, or more than two characters long */
                LOG("argparse: invalid argument %s\n", argv[i]);
                displayErrorMessage("invalid argument");
                showUsage();
                return -1;
            }
            switch (argv[i][1]) { /* decide what to do depending on the letter */
                case 'n':
                    LOG("argparse: don't invoke exit\n");
                    staOptions &= ~INVOKE_EXIT;
                    break;
                case 's':
                    LOG("argparse: power off\n");
                    staOptions |= POWER_OFF;
                    break;
                case 'f':
                    LOG("argparse: full flash\n");
                    staOptions |= FULL_FLASH;
                    break;
                case 'y':
                    LOG("argparse: no warnings\n");
                    staOptions |= NO_WARNINGS;
                    break;
                case 'p':
                    filePath = argv[++i];
                    LOG("argparse: new file path is %s\n", argv[i]);
                    break;
                case 'a':
                    LOG("argparse: flash a\n");
                    if (partitionName != BOOT) {
                        LOG("argparse: flash both\n");
                        staOptions |= BOTH_FLASH;
                        partitionName = BOOT_A;
                        continue;
                    }
                    staOptions |= SLOT_FLASH;
                    partitionName = BOOT_A;
                    break;
                case 'b':
                    LOG("argparse: flash b\n");
                    if (partitionName != BOOT) {
                        LOG("argparse: flash both\n");
                        staOptions |= BOTH_FLASH;
                        partitionName = BOOT_A;
                        continue;
                    }
                    staOptions |= SLOT_FLASH;
                    partitionName = BOOT_B;
                    break;
                case 'h':
                    LOG("argparse: show help\n");
                    showUsage();
                    return 0;
                default:
                    LOG("argpase: invalid argument %s\n", argv[i]);
                    displayErrorMessage("invalid argument");
                    showUsage();
                    return -1;
            }
        }
    }
    LOG("past argparse\n");

    LOG("allocating buffer of %u bytes\n", BUFFER_SIZE);
    buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        LOG("failed to allocate buffer\n");
        displayErrorMessage("failed to allocate memory");
        return -1;
    }

    LOG("opening %s\n", filePath);
    hInput = CreateFileA(
        filePath,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );
    if (hInput == INVALID_HANDLE_VALUE && filePath == BOOT_PATH) {
        LOG("failed to open %s\ntrying C:\\boot.img.img\n", filePath);
        hInput = CreateFileA("C:\\boot.img.img", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hInput != INVALID_HANDLE_VALUE) {
            LOG("found C:\\boot.img.img, exiting\n");
            CloseHandle(hInput);
            free(buffer);
            displayErrorMessage("your boot image in C:\\ is currently named \"boot.img.img\".\r\nplease rename it to \"boot.img\".\r\nby default, windows hides file extensions, so it may seem to you that the file is already named that, while it in fact is not.");
            return -1;
        }
        LOG("C:\\boot.img.img not found\n");
    }
    if (hInput == INVALID_HANDLE_VALUE) {
        char failMsg[28 + MAX_PATH + 10];
        DWORD errorCode = GetLastError();
        free(buffer);
        LOG("failed to open input, code %lu\n", errorCode);
        if (errorCode == ERROR_FILE_NOT_FOUND) {
            sprintf(failMsg, "%s not found", filePath);
        } else {
            sprintf(failMsg, "failed to open %s. error code %lu", filePath, errorCode);
        }
        displayErrorMessage(failMsg);
        return -1;
    }

    LOG("parsing input\n");
    GetFileSizeEx(hInput, &fileSize);
    LOG("file size is %lli\n", fileSize.QuadPart);
    if (!(staOptions & FULL_FLASH)) { /* if -f was not specified,
                                       * try to treat the file as an android boot image and parse its size */
        LOG("parsing actual boot image size\n");
        flashSize = parseBootImageSize(hInput);
        LOG("parsed size: %llu\n", flashSize);
        if (!flashSize) {
            LOG("failed to parse boot image size\n");
            if (!(staOptions & NO_WARNINGS)) {
                int ret;
                char msg[85 + MAX_PATH];
                LOG("displaying warning\n");
                sprintf(msg, "ERROR! %s is not an android boot image.\r\npress OK to continue, or press Cancel to abort.", filePath);
                ret = MessageBoxA(NULL, msg, ACTION_TITLE, MB_OKCANCEL | MB_ICONEXCLAMATION);
                if (ret == IDCANCEL) {
                    LOG("user pressed cancel\n");
                    CloseHandle(hInput);
                    free(buffer);
                    DEBUGPAUSE();
                    return -1;
                }
            }
            LOG("using file size as flashsize\n");
            flashSize = fileSize.QuadPart;
        } else if ((ULONGLONG)fileSize.QuadPart < flashSize) {
            LOG("file size is less than parsed image size\n");
            CloseHandle(hInput);
            free(buffer);
            displayErrorMessage("what the HECK is wrong with your boot image?!");
            return -1;
        }
        LOG("resetting file pointer\n");
        SetFilePointer(hInput, 0, NULL, FILE_BEGIN);
    } else {
        LOG("skipped parsing boot image size\n");
        flashSize = fileSize.QuadPart;
    }

    LOG("enumerating partitions\n");
    diskAmount = enumeratePartitions(&partitionLists);
    if (diskAmount == 0) {
        LOG("enumeration failed\n");
        free(buffer);
        displayErrorMessage("failed to enumerate partitions");
        return -1;
    }

    for (;;) {
        unsigned i, j;
        HANDLE hPartition;
        DWORD sectorSize;
        for (i = 0; i < diskAmount; i++) {
            for (j = 0; j < partitionLists[i].amount; j++) {
                LOG("testing partition %u of disk %u\n", j, i);
                if (strcmp(partitionLists[i].partitionNames[j], partitionName) == 0) {
                    LOG("found %s\n", partitionName);
                    sprintf(partitionPath, "\\\\.\\Harddisk%uPartition%u", (unsigned char)partitionLists[i].disk, (unsigned char)j+1);
                    LOG("%s\n", partitionPath);
                    if ((partitionName == BOOT_A || partitionName == BOOT_B) && !(staOptions & (SLOT_FLASH | BOTH_FLASH))) {
                        PARTITION_INFORMATION_EX partitionInformationEx;
                        DWORD dwTemp;
                        unsigned char abAttributes;
                        HANDLE hPartition = CreateFileA(partitionPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                        LOG("testing if active\n");
                        if (hPartition == INVALID_HANDLE_VALUE) {
                            char failMsg[34 + sizeof(partitionPath) + 10];
                            DWORD errorCode = GetLastError();
                            LOG("failed to open %s, code %lu\n", partitionPath, errorCode);
                            CloseHandle(hInput);
                            free(buffer);
                            free(partitionLists);
                            sprintf(failMsg, "failed to open drive %s. error code %lu", partitionPath, errorCode);
                            displayErrorMessage(failMsg);
                            return -1;
                        }
                        if (!DeviceIoControl(hPartition, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &partitionInformationEx, sizeof(partitionInformationEx), &dwTemp, NULL)) {
                            char failMsg[45 + sizeof(partitionPath) + 10];
                            DWORD errorCode = GetLastError();
                            LOG("failed to obtain info for %s, code %lu\n", partitionPath, errorCode);
                            CloseHandle(hInput);
                            CloseHandle(hPartition);
                            free(buffer);
                            free(partitionLists);
                            sprintf(failMsg, "failed to obtain info for drive %s. error code %lu", partitionPath, errorCode);
                            displayErrorMessage(failMsg);
                            return -1;
                        }
                        if (partitionInformationEx.PartitionStyle != PARTITION_STYLE_GPT) {
                            LOG("not gpt, exiting\n");
                            CloseHandle(hInput);
                            CloseHandle(hPartition);
                            free(buffer);
                            free(partitionLists);
                            displayErrorMessage("not gpt");
                            return -1;
                        }
                        abAttributes = *((char*)&partitionInformationEx.Gpt.Attributes+AB_FLAG_OFFSET);
                        LOG("abAttributes: %u\n", abAttributes);
                        if (!(abAttributes & AB_PARTITION_ATTR_SLOT_ACTIVE)) {
                            LOG("not active\n");
                            if (partitionName == BOOT_B) {
                                LOG("boot_b is not active, exiting\n");
                                CloseHandle(hInput);
                                CloseHandle(hPartition);
                                free(buffer);
                                free(partitionLists);
                                displayErrorMessage("failed to find an active boot partition");
                                return -1;
                            }
                            LOG("trying boot_b\n");
                            partitionName = BOOT_B;
                            continue;
                        }
                        CloseHandle(hPartition);
                    }
                    LOG("found boot partition, proceeding\n");
                    goto foundPartition;
                }
            }
        }
        if (partitionName != BOOT) {
            LOG("failed to find boot partition (boot_a)\n");
            CloseHandle(hInput);
            free(buffer);
            free(partitionLists);
            displayErrorMessage("failed to find the boot partition");
            return -1;
        }
        LOG("failed to find boot, trying boot_a\n");
        partitionName = BOOT_A;
        continue;
foundPartition:

        LOG("opening %s\n", partitionPath);
        hPartition = CreateFileA(
            partitionPath,
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
            NULL
        );
        if (hPartition == INVALID_HANDLE_VALUE) {
            char failMsg[34 + sizeof(partitionPath) + 10];
            DWORD errorCode = GetLastError();
            LOG("failed to open %s, code %lu\n", partitionPath, errorCode);
            CloseHandle(hInput);
            free(buffer);
            free(partitionLists);
            sprintf(failMsg, "failed to open drive %s. error code %lu", partitionPath, errorCode);
            displayErrorMessage(failMsg);
            return -1;
        }

        LOG("obtaining sector size\n");
        sectorSize = getSectorSize(hPartition);
        if (!sectorSize) {
            LOG("failed to get sector size\n");
            CloseHandle(hPartition);
            CloseHandle(hInput);
            free(partitionLists);
            free(buffer);
            return -1;
        }

        LOG("flashing %llu bytes\n", flashSize);
        if (!flashPartition(hPartition, hInput, sectorSize, flashSize, buffer, BUFFER_SIZE)) {
            char failMsg[16 + 6 + 1];
            LOG("flashing failed\n");
            CloseHandle(hInput);
            CloseHandle(hPartition);
            free(buffer);
            free(partitionLists);
            sprintf(failMsg, "failed to flash %s", partitionName);
            displayErrorMessage(failMsg);
            return -1;
        }
        LOG("flashing complete, resettng file pointer\n");
        SetFilePointer(hInput, 0, NULL, FILE_BEGIN);
        CloseHandle(hPartition);
        if (staOptions & BOTH_FLASH && partitionName == BOOT_A) {
            LOG("both_flash detected, continuing with boot_b");
            partitionName = BOOT_B;
            continue;
        }
        LOG("flashing done\n");
        break;
    }

    CloseHandle(hInput);
    free(partitionLists);
    free(buffer);

    LOG("exiting windows (shutdown: %u)\n", staOptions & INVOKE_EXIT);
    if (staOptions & INVOKE_EXIT && !exitWindows(staOptions & POWER_OFF)) {
        char failMsg[35 + 1 + 10];
        DWORD errorCode = GetLastError();
        LOG("failed to exit windows, code %lu\n", errorCode);
        sprintf(failMsg, "failed to exit windows. error code %lu", errorCode);
        displayErrorMessage(failMsg);
        return -1;
    }

    DEBUGPAUSE();
    return 0;
}
