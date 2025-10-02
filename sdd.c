#define __USE_MINGW_ANSI_STDIO 0
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define TITLE "sdd"
#define ADDITIONAL_HELP "[-c <CONFIGPATH>] Specify a custom path to the config"
#include "sta2.h"

const char *STA_CONF = "sta.conf";
const char *SDD_CONF = "sdd.conf";

char *my_fgets(char *str, int num, FILE *stream) /* removes LF tags and skips to next line if the buffer wasn't big enough to read it whole */
{
    char *ret = fgets(str, num, stream); /* invoke fgets */
    if (ret) { /* only alter the output if the function succeded */
        size_t lastIndex = strlen(str) - 1; /* strlen(str) can never return 0 in this case,
                                             * as fgets() will return a NULL pointer if no characters were read */
        if (str[lastIndex] == '\n') { /* remove LF if present */
            str[lastIndex] = '\0';
        } else { /* else, go to the next line */
            int c;
            while ((c = getc(stream)) != '\n' && c != EOF);
        }
    }
    return ret; 
}

int fillPartitionWithZeros(HANDLE hPartition, char *buffer, size_t bufferSize)
{
    ULONGLONG bytesLeftToWrite;
    memset(buffer, 0, bufferSize);
    {
        PARTITION_INFORMATION_EX partitionInfo;
        DWORD dwTemp;
        if(!DeviceIoControl(hPartition, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &partitionInfo, sizeof(partitionInfo), &dwTemp, NULL)) {
            displayErrorMessage("failed to retrieve partition information");
            return 0;
        }
        bytesLeftToWrite = partitionInfo.PartitionLength.QuadPart;
        LOG("bytesLeftToWrite: %llu\n", bytesLeftToWrite);
    }
    while (bytesLeftToWrite != 0) {
        DWORD bytesWritten;
        DWORD bytesToWrite = bytesLeftToWrite < bufferSize ? bytesLeftToWrite : bufferSize;
        LOG("bytesToWrite: %lu\n", bytesToWrite);
        WriteFile(hPartition, buffer, bytesToWrite, &bytesWritten, NULL);
        if (bytesToWrite != bytesWritten) {
            LOG("failed. error code %lu\n", GetLastError());
            return 0;
        }
        bytesLeftToWrite -= bytesWritten;
        LOG("wrote %lu, %llu bytes left to write\n", bytesWritten, bytesLeftToWrite);
    }
    return 1;
}

int main(int argc, char *argv[])
{
    FILE *configFile;
    int staOptions = INVOKE_EXIT;
    const char *configFilePath = SDD_CONF;
    partitionList *partitionLists = NULL;
    unsigned diskAmount = 0;

    /* parse the arguments */
    {
        int i;
        for (i = 1; i < argc; i++) {
            /* make sure the supplied value is a valid argument */
            if (argv[i][0] != '-' || argv[i][1] == '\0' || argv[i][2] != '\0') { /* stop on args that do not start with a dash (-), are one character long, or more than two characters long */
                displayErrorMessage("invalid argument");
                showUsage();
                return -1;
            }
            switch (argv[i][1]) { /* decide what to do depending on the letter */
                case 'n':
                    staOptions &= ~INVOKE_EXIT;
                    break;
                case 's':
                    staOptions |= POWER_OFF;
                    break;
                case 'f':
                    staOptions |= FULL_FLASH;
                    break;
                /* case 'y':
                    staOptions |= NO_WARNINGS;
                    break; */
                case 'c':
                    configFilePath = argv[++i]; /* also skips parsing the next argument (which is the config path)
                                                 * by incrementing i */
                    break;
                case 'h':
                    showUsage();
                    return 0;
                default:
                    displayErrorMessage("invalid argument");
                    showUsage();
                    return -1;
            }
        }
    }

    configFile = fopen(configFilePath, "r");
    if (!configFile && configFilePath == SDD_CONF) {
        configFilePath = STA_CONF;
        configFile = fopen(configFilePath, "r");
        if (configFile) {
            displayWarningMessage("sta.conf is not supported. switch to sdd.conf");
        }
    }
    if (!configFile) {
        displayErrorMessage("failed to open config");
        return -1;
    }

    {
        char partition[MAX_PARTITION_NAME_LEN + 1]; /* an extra character for the underscore */
        char *buffer = malloc(BUFFER_SIZE);
        if (!buffer) {
            displayErrorMessage("failed to allocate memory");
            return -1;
        }
        while (memset(partition, 0, sizeof(partition)), my_fgets(partition, sizeof(partition), configFile)) {
            char filePath[MAX_PATH];
            char partitionPath[28];
            HANDLE hPartition;
            DWORD sectorSize, dwTemp;

            if (partition[0] == '\0' || partition[0] == ':' || partition[0] == '#') continue; /* skip empty lines and comments */

            if (partition[0] == '_') {
                unsigned i, j;
                if (!partitionLists) {
                    diskAmount = enumeratePartitions(&partitionLists);
                    if (diskAmount == 0) {
                        free(buffer);
                        displayErrorMessage("failed to enumerate partitions");
                        return -1;
                    }
                }
                for (i = 0; i < diskAmount; i++) {
                    for (j = 0; j < partitionLists[i].amount; j++) {
                        if (strcmp(partitionLists[i].partitionNames[j], partition+1) == 0) {
                            sprintf(partitionPath, "\\\\.\\Harddisk%uPartition%u", (unsigned char)partitionLists[i].disk, (unsigned char)j+1);
                            goto foundPartition;
                        }
                    }
                }
                {
                    char failMsg[15 + MAX_PARTITION_NAME_LEN + 1];
                    free(buffer);
                    free(partitionLists);
                    sprintf(failMsg, "failed to find %s", partition+1);
                    displayErrorMessage(failMsg);
                    return -1;
                }
            } else if (partition[0] == 's' && partition[1] == 'd') {
                char diskLetter = partition[2];
                unsigned char diskNumber;
                if (diskLetter < 'a' || diskLetter > 'z') {
                    free(buffer);
                    free(partitionLists);
                    displayErrorMessage("invalid disk letter");
                    return -1;
                }
                diskNumber = diskLetter - 'a'; /* the format is sdXNNN, where X is the disk letter */
                if (partition[3] != '\0') {
                    sprintf(partitionPath, "\\\\.\\Harddisk%uPartition%.3s", (unsigned char)diskNumber, partition+3);
                } else {
                    sprintf(partitionPath, "\\\\.\\PhysicalDrive%u", (unsigned char)diskNumber);
                }
            } else {
                char failMsg[sizeof(partition) + 25];
                free(buffer);
                free(partitionLists);
                sprintf(failMsg, "%s is not a valid partition", partition);
                displayErrorMessage(failMsg);
                return -1;
            }
foundPartition:

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
                free(buffer);
                free(partitionLists);
                sprintf(failMsg, "failed to open drive %s. error code %lu", partitionPath, GetLastError());
                displayErrorMessage(failMsg);
                return -1;
            }

            sectorSize = getSectorSize(hPartition);
            if (!sectorSize) {
                CloseHandle(hPartition);
                free(partitionLists);
                free(buffer);
                return -1;
            }

            if (!DeviceIoControl(hPartition, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &dwTemp, NULL)) {
                char failMsg[22 + sizeof(partitionPath) + 13 + 10];
                sprintf(failMsg, "failed to lock volume %s. error code %lu", partitionPath, GetLastError());
                displayWarningMessage(failMsg);
            }
            if (!DeviceIoControl(hPartition, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &dwTemp, NULL)) {
                char failMsg[26 + sizeof(partitionPath) + 13 + 10];
                sprintf(failMsg, "failed to dismount volume %s. error code %lu", partitionPath, GetLastError());
                displayWarningMessage(failMsg);
            }

            while (*filePath = '\0', my_fgets(filePath, sizeof(filePath), configFile)) { /* retrieve the file path */
                if (filePath[0] == '\0' || filePath[0] == ':' || filePath[0] == '#') continue;
                break;
            }
            if (filePath[0] == '\0') {
                displayErrorMessage("expected a file path after the last partition name");
                CloseHandle(hPartition);
                free(partitionLists);
                free(buffer);
                return -1;
            }
            if (filePath[0] == '-') {
                if (partition[3] == '\0') {
                    displayErrorMessage("are you out of your mind? can't erase disks.");
                    CloseHandle(hPartition);
                    free(partitionLists);
                    free(buffer);
                    return -1;
                }
                if (!fillPartitionWithZeros(hPartition, buffer, BUFFER_SIZE)) {
                    char failMsg[26 + sizeof(partition)];
                    CloseHandle(hPartition);
                    free(buffer);
                    free(partitionLists);
                    sprintf(failMsg, "failed to erase partition %s", partition);
                    displayErrorMessage(failMsg);
                    return -1;
                }
            } else {   
                LARGE_INTEGER fileSize;
                ULONGLONG flashSize;
                HANDLE hInput;

                hInput = CreateFileA(
                    filePath,
                    GENERIC_READ,
                    FILE_SHARE_READ,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                    NULL
                );
                if (hInput == INVALID_HANDLE_VALUE) {
                    char failMsg[33 + sizeof(filePath) + 10];
                    DWORD errorCode = GetLastError();
                    CloseHandle(hPartition);
                    free(buffer);
                    free(partitionLists);
                    if (errorCode == ERROR_FILE_NOT_FOUND) {
                        sprintf(failMsg, "%s not found", filePath);
                    } else {
                        sprintf(failMsg, "failed to open file %s. error code %lu", filePath, errorCode);
                    }
                    displayErrorMessage(failMsg);
                    return -1;
                }

                GetFileSizeEx(hInput, &fileSize);
                if (!(staOptions & FULL_FLASH)) { /* if -f was not specified,
                                                   * try to treat the file as an android boot image and parse its size */
                    flashSize = parseBootImageSize(hInput);
                    if (!flashSize) {
                        /* if (!(staOptions & NO_WARNINGS)) {
                            int ret;
                            char msg[85 + MAX_PATH];
                            sprintf(msg, "ERROR! %s is not an android boot image.\npress OK to continue, or press Cancel to abort.", filePath);
                            ret = MessageBoxA(NULL, msg, ACTION_TITLE, MB_OKCANCEL | MB_ICONEXCLAMATION);
                            if (ret == IDCANCEL) {
                                CloseHandle(hInput);
                                free(buffer);
                                return -1;
                            }
                        } */
                        flashSize = fileSize.QuadPart;
                    } else if ((ULONGLONG)fileSize.QuadPart < flashSize) {
                        CloseHandle(hInput);
                        free(buffer);
                        displayErrorMessage("what the HECK is wrong with your boot image?!");
                        return -1;
                    }
                    SetFilePointer(hInput, 0, NULL, FILE_BEGIN);
                } else {
                    flashSize = fileSize.QuadPart;
                }

                if (!flashPartition(hPartition, hInput, sectorSize, flashSize, buffer, BUFFER_SIZE)) {
                    char failMsg[16 + sizeof(partition)];
                    CloseHandle(hInput);
                    CloseHandle(hPartition);
                    free(buffer);
                    free(partitionLists);
                    sprintf(failMsg, "failed to flash %s", partition);
                    displayErrorMessage(failMsg);
                    return -1;
                }

                CloseHandle(hInput);
            }

            CloseHandle(hPartition);
        }
        free(partitionLists);
        free(buffer);
    }

    if (staOptions & INVOKE_EXIT && !exitWindows(staOptions & POWER_OFF)) {
        char failMsg[35 + 1 + 10];
        sprintf(failMsg, "failed to exit windows. error code %lu", GetLastError());
        displayErrorMessage(failMsg);
        return -1;
    }

    return 0;
}
