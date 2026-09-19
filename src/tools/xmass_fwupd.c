// Copyright (c) 2026 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

#define PCIE_XMASS_DRIVER_MAGIC 0xDC
#define PCIE_FLASH_READ  _IOWR(PCIE_XMASS_DRIVER_MAGIC, 1, uint8_t*)
#define PCIE_FLASH_ERASE _IOWR(PCIE_XMASS_DRIVER_MAGIC, 2, uint32_t)
#define PCIE_FLASH_WRITE _IOWR(PCIE_XMASS_DRIVER_MAGIC, 3, uint8_t*)
#define PCIE_GETIDS      _IOWR(PCIE_XMASS_DRIVER_MAGIC, 4, uint8_t*)

#define BUFFER_SIZE 65536

int main(int argc, char *argv[])
{
    int fd;
    uint32_t magic = 0;
    uint8_t buffer[BUFFER_SIZE];
    uint8_t rb_buffer[BUFFER_SIZE];
    uint8_t info[10];
    int do_info = 0;

    const char* device_name = (argc >= 3) ? argv[2] : "/dev/xmass0";

    if (argc < 2) {
        fprintf(stderr, "Usage: %s filename.bin [/dev/xmass0]\n", argv[0]);
        fprintf(stderr, "Usage: %s info -- display actual firmware version\n", argv[0]);
        return EXIT_FAILURE;
    }

    do_info = (strcmp(argv[1], "info") == 0);
    if (!do_info) {
        FILE *file = fopen(argv[1], "rb");
        if (file == NULL) {
            perror("Error opening file");
            return EXIT_FAILURE;
        }

        size_t bytesRead = fread(buffer, 1, BUFFER_SIZE, file);
        printf("Read %zu bytes from %s\n", bytesRead, argv[1]);

        fclose(file);

        if (bytesRead != BUFFER_SIZE) {
            fprintf(stderr, "File is too small, only got %zd bytes!\n", bytesRead);
            return EXIT_FAILURE;
        }
    }

    fd = open(device_name, O_RDWR);
    if (fd < 0) {
        perror("Could't open device");
        return EXIT_FAILURE;
    }

    if (ioctl(fd, PCIE_GETIDS, info) < 0) {
        perror("Unable to get ids");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("ASM2806 Firmware in use: %02x%02x%02x%02x%02x%02x, FlashID: %02x %02x %02x\n",
           info[0], info[1], info[2], info[3], info[4], info[5],
           info[6], info[7], info[8]);

    if (do_info) {
        close(fd);
        return EXIT_SUCCESS;
    }

    printf("Opened %s, flashing %s\n", device_name, argv[1]);
    printf("Erasing flash...\n");

    if (ioctl(fd, PCIE_FLASH_ERASE, magic) < 0) {
        perror("Erasing flash error");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Writing new image...\n");
    if (ioctl(fd, PCIE_FLASH_WRITE, buffer) < 0) {
        perror("Writing flash error");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Verifying image...\n");
    if (ioctl(fd, PCIE_FLASH_READ, rb_buffer) < 0) {
        perror("Readback flash error");
        close(fd);
        return EXIT_FAILURE;
    }

    if (memcmp(buffer, rb_buffer, 0xE00) || memcmp(buffer + 0xF00, rb_buffer + 0xF00, BUFFER_SIZE - 0xF00)) {
        fprintf(stderr, "Verification failed!\n");
        return EXIT_FAILURE;
    }

    close(fd);
    return EXIT_SUCCESS;
}
