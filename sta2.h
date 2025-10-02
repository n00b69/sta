#ifndef TITLE
#define TITLE "sta2"
#endif

#define ERR_TITLE TITLE" - error"
#define ACTION_TITLE TITLE" - what will you do?"

#ifndef ADDITIONAL_HELP
#define ADDITIONAL_HELP ""
#endif

#include <stdio.h>
#include <string.h>
#include <windows.h>

#ifdef DEBUG
#define DEBUGPAUSE() system("pause")
#define LOG(...) printf(__VA_ARGS__)
#else
#define DEBUGPAUSE() do {} while (0)
#define LOG(...) do {} while (0)
#endif

#define BUFFER_SIZE 4 * 1024 * 1024 /* allocated on the HEAP. 4MiB seems the most effective, at least on my PC */
#define MAX_PARTITION_ENTRIES_NUM 128
#define MAX_PARTITION_NAME_LEN 36 /* as found in PARTITION_INFORMATION_GPT */
#define DRIVE_LAYOUT_INFORMATION_EX_SIZE sizeof(DRIVE_LAYOUT_INFORMATION_EX) + (MAX_PARTITION_ENTRIES_NUM - 1)*sizeof(PARTITION_INFORMATION_EX)

unsigned parseBootImageSize(HANDLE bootImage);

#define displayErrorMessage(msg) MessageBoxA(NULL, msg, ERR_TITLE, MB_OK | MB_ICONERROR)
#define displayInfoMessage(msg) MessageBoxA(NULL, msg, TITLE, MB_OK | MB_ICONINFORMATION)
#define displayWarningMessage(msg) MessageBoxA(NULL, msg, TITLE, MB_OK | MB_ICONWARNING)

typedef struct partitionList {
    unsigned amount;
    unsigned disk;
    char partitionNames[MAX_PARTITION_ENTRIES_NUM][MAX_PARTITION_NAME_LEN];
} partitionList;

enum staOptions {
    INVOKE_EXIT = 1u << 0,
    POWER_OFF = 1u << 1,
    FULL_FLASH = 1u << 2,
    NO_WARNINGS = 1u << 3,
    SLOT_FLASH = 1u << 4,
    BOTH_FLASH = 1u << 5
};

void showUsage(void)
{
    displayInfoMessage("Usage:\r\n"
    "\r\n"
    "[-h] Show this message\r\n"
    "[-s] Shut down instead of rebooting\r\n"
    "[-n] Do not reboot/shut down after flashing\r\n"
    "[-f] Flash the full boot image file instead of just the payload\r\n"
    ADDITIONAL_HELP);
}

unsigned enumeratePartitions(partitionList **list)
{
    unsigned arrayLength = 0, i, j;
    DWORD dwTemp;
    partitionList *partitionLists = NULL;
    DRIVE_LAYOUT_INFORMATION_EX *pDriveLayoutInformationEx = malloc(DRIVE_LAYOUT_INFORMATION_EX_SIZE);
    if (!pDriveLayoutInformationEx) {
        LOG("failed to allocate memory for drive layout information ex\n");
        displayErrorMessage("malloc failed");
        return 0;
    }
    for (i = 0; i < 256; i++) {
        char diskPath[21];
        HANDLE hDisk;
        sprintf(diskPath, "\\\\.\\PhysicalDrive%u", (unsigned char)i);
        LOG("opening %s (disk %u)\n", diskPath, i);
        hDisk = CreateFileA(diskPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hDisk == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            LOG("failed to open disk, code %lu\n", error);
            if (error == ERROR_FILE_NOT_FOUND) {
                LOG("ran out of disks, breaking\n");
                break;
            }
            {
                char failMsg[28 + sizeof(diskPath) + 10];
                free(pDriveLayoutInformationEx);
                sprintf(failMsg, "failed to open disk %s. error %lu", diskPath, error);
                displayErrorMessage(failMsg);
                return 0;
            }
        }
        if (!DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, pDriveLayoutInformationEx, DRIVE_LAYOUT_INFORMATION_EX_SIZE, &dwTemp, NULL)) {
            char failMsg[25 + sizeof(diskPath)];
            LOG("failed to read layout, code %lu\n", GetLastError());
            free(pDriveLayoutInformationEx);
            CloseHandle(hDisk);
            sprintf(failMsg, "failed to read layout of %s", diskPath);
            displayErrorMessage(failMsg);
            return 0;
        }
        CloseHandle(hDisk);
        if (pDriveLayoutInformationEx->PartitionStyle != PARTITION_STYLE_GPT) {
            LOG("not gpt\n");
            continue;
        }
        ++arrayLength;
        partitionLists = realloc(partitionLists, sizeof(*partitionLists)*arrayLength);
        if (!partitionLists) {
            LOG("failed to realloc partitionLists, arrayLength was %u\n", arrayLength);
            free(pDriveLayoutInformationEx);
            displayErrorMessage("realloc failed");
            return 0;
        }
        partitionLists[i].disk = i;
        partitionLists[i].amount = (unsigned)pDriveLayoutInformationEx->PartitionCount;
        LOG("%u partitions on disk %u\n", partitionLists[i].amount, partitionLists[i].disk);
        for (j = 0; j < partitionLists[i].amount; j++) {
            wchar_t *wPartitionName = pDriveLayoutInformationEx->PartitionEntry[j].Gpt.Name;
            WideCharToMultiByte(CP_ACP, 0, wPartitionName, MAX_PARTITION_NAME_LEN, partitionLists[i].partitionNames[j], MAX_PARTITION_NAME_LEN, NULL, NULL);
            LOG("partition %u is %s\n", j, partitionLists[i].partitionNames[j]);
        }
    }
    free(pDriveLayoutInformationEx);
    *list = partitionLists;
    LOG("done enumerating partitions\n");
    return arrayLength;
}

int exitWindows(int powerOff)
{
    HANDLE tokenHandle;
    TOKEN_PRIVILEGES tkp;
    int exitFlags = EWX_FORCE | EWX_FORCEIFHUNG;

    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &tokenHandle)) {
        LOG("exitWindows: failed to open process token, code %lu\n", GetLastError());
        return 0;
    }
    if(!LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid)) {
        LOG("exitWindows: failed to find value of SE_SHUTDOWN_NAME, code %lu\n", GetLastError());
        return 0;
    }
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if(!AdjustTokenPrivileges(tokenHandle, FALSE, &tkp, 0, NULL, NULL)) {
        LOG("exitWindows: failed to adjust token privileges, code %lu\n", GetLastError());
        return 0;
    }
    CloseHandle(tokenHandle);

    exitFlags |= powerOff ? EWX_POWEROFF : EWX_REBOOT;
    if(!ExitWindowsEx(exitFlags, 0)) {
        LOG("exitWindows: failed to exit windows, code %lu\n", GetLastError());
        return 0;
    }

    LOG("exitWindows: done\n");
    return 1;
}

/* (re)setting the input file pointer is on the CALLER */
int flashPartition(HANDLE hPartition, HANDLE hInput, size_t sectorSize, ULONGLONG inputSize, char *buffer, size_t bufferSize)
{
    ULONGLONG totalRead = 0;
    int excessData;
    {
        int remainder = inputSize % sectorSize;
        excessData = remainder ? sectorSize - remainder : 0;
        LOG("remainder: %i, excess data: %i\n", remainder, excessData);
    }
    while (totalRead != inputSize) {
        DWORD bytesRead, bytesWritten;
        if(!ReadFile(hInput, buffer, bufferSize, &bytesRead, NULL)) {
            char failMsg[28 + 10];
            DWORD errorCode = GetLastError();
            LOG("failed to read, code %lu", errorCode);
            sprintf(failMsg, "reading failed, error code %lu", errorCode);
            displayErrorMessage(failMsg);
            return 0;
        }
        totalRead += bytesRead;
        LOG("read %lu, total %llu\n", bytesRead, totalRead);
        if (bytesRead < bufferSize) {
            LOG("read less than buffer (%zu). current bytesRead: %lu\n", bufferSize, bytesRead);
            memset(buffer+bytesRead, 0, excessData);
            bytesRead += excessData;
            LOG("filled %i bytes with zeros, new bytesRead: %lu\n", excessData, bytesRead);
        } else if (totalRead > inputSize) {
            LOG("read more than expected, bytesRead: %lu\n", bytesRead);
            bytesRead -= (totalRead - inputSize);
            totalRead = inputSize;
            if (excessData) {
                LOG("!! excess data found in boot image payload. potential UEFI image\n");
                memset(buffer+bytesRead, 0, excessData);
                bytesRead += excessData;
                LOG("filled %i bytes with zeros, new bytesRead: %lu\n", excessData, bytesRead);
            }
            LOG("new bytesRead: %lu\n", bytesRead);
        }
        WriteFile(hPartition, buffer, bytesRead, &bytesWritten, NULL);
        if (bytesRead != bytesWritten) {
            char failMsg[28 + 10];
            DWORD errorCode = GetLastError();
            LOG("failed to write, code %lu\n", errorCode);
            sprintf(failMsg, "writing failed, error code %lu", errorCode);
            displayErrorMessage(failMsg);
            return 0;
        }
    }
    return 1;
}

DWORD getSectorSize(HANDLE hDisk)
{
    DISK_GEOMETRY diskGeometry;
    DWORD dwTemp;
    if(!DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &diskGeometry, sizeof(diskGeometry), &dwTemp, NULL)) {
        LOG("failed to get sector size, code %lu\n", GetLastError());
        displayErrorMessage("failed to retrieve sector size");
        return 0;
    }
    LOG("sector size is %lu\n", diskGeometry.BytesPerSector);
    return diskGeometry.BytesPerSector;
}
