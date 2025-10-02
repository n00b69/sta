#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <windows.h>
#include "bootimg.h"

#ifdef BOOTIMGSIZE_MAIN
#include <stdio.h>
#endif

/* (re)setting the input file pointer is on the CALLER */
extern "C" uint32_t parseBootImageSize(HANDLE bootImage)
{
    BOOL read;
    DWORD bytesRead;
    char magic[BOOT_MAGIC_SIZE];
    read = ReadFile(bootImage, magic, BOOT_MAGIC_SIZE, &bytesRead, NULL);
    if (memcmp(magic, BOOT_MAGIC, BOOT_MAGIC_SIZE) != 0) { // make sure that the provided file is an android boot image
        return 0;
    }
    uint32_t headerVersion;
    SetFilePointer(bootImage, offsetof(struct boot_img_hdr_v0, header_version), NULL, FILE_BEGIN);
    read = ReadFile(bootImage, &headerVersion, sizeof(uint32_t), &bytesRead, NULL);
    if (!read) {
        return 0;
    }
    union {
        struct boot_img_hdr_v0 v0;
        struct boot_img_hdr_v1 v1;
        struct boot_img_hdr_v2 v2;
        struct boot_img_hdr_v3 v3;
        struct boot_img_hdr_v4 v4;
    } bootImageHeader;
    size_t bootImageHeaderSize;
    switch (headerVersion) {
        case 0:
            bootImageHeaderSize = sizeof(struct boot_img_hdr_v0);
            break;
        case 1:
            bootImageHeaderSize = sizeof(struct boot_img_hdr_v1);
            break;
        case 2:
            bootImageHeaderSize = sizeof(struct boot_img_hdr_v2);
            break;
        case 3:
            bootImageHeaderSize = sizeof(struct boot_img_hdr_v3);
            break;
        case 4:
            bootImageHeaderSize = sizeof(struct boot_img_hdr_v4);
            break;
        default:
            return 0;
    }
    SetFilePointer(bootImage, 0, NULL, FILE_BEGIN);
    read = ReadFile(bootImage, &bootImageHeader, bootImageHeaderSize, &bytesRead, NULL);
    if (!read) return 0;
    uint32_t pageSize;
    uint32_t sizes[6] = {0};
    switch (headerVersion) {
        case 2:
            sizes[5] = bootImageHeader.v2.dtb_size;
        case 1:
            sizes[4] = bootImageHeader.v1.recovery_dtbo_size;
            sizes[3] = bootImageHeader.v1.header_size;
        case 0:
            if (headerVersion == 0) sizes[3] = sizeof(struct boot_img_hdr_v0); // v0 does not have a field for the header size, and we do want to count it
            sizes[2] = bootImageHeader.v0.kernel_size;
            sizes[1] = bootImageHeader.v0.ramdisk_size;
            sizes[0] = bootImageHeader.v0.second_size;
            pageSize = bootImageHeader.v0.page_size;
            break;
        case 4:
            sizes[3] = bootImageHeader.v4.signature_size;
        case 3:
            sizes[2] = bootImageHeader.v3.kernel_size;
            sizes[1] = bootImageHeader.v3.ramdisk_size;
            sizes[0] = bootImageHeader.v3.header_size;
            pageSize = 4096;
    }
    uint32_t totalSize = 0;
    for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); i++) {
        totalSize += sizes[i];
        uint32_t remainder = sizes[i] % pageSize;
        if (remainder) totalSize += pageSize - remainder;
    }
    return totalSize;
}

#ifdef BOOTIMGSIZE_MAIN
extern "C" int main(int argc, char *argv[])
{
    if (argc != 2) {
        printf("usage: %s <PATH TO BOOT IMAGE>\n", argv[0]);
        return 1;
    }
    HANDLE bootImage = CreateFileA(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (bootImage == INVALID_HANDLE_VALUE) {
        printf("failed to open %s\n", argv[1]);
        return 1;
    }
    uint32_t size = parseBootImageSize(bootImage);
    CloseHandle(bootImage);
    if (!size) {
        printf("err\n");
        return 1;
    }

    printf("%u\n", size);
    return 0;
}
#endif
