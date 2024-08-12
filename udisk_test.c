#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>

#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/random.h>

#include <xxhash.h>

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stdout, "usage: %s /dev/sdX\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "failed to open device %s (errno %d)\n", argv[1], errno);
        return 1;
    }
    uint32_t write_sectors = strtol(argv[2], NULL, 10);
    if (write_sectors == 0)
    {
        fprintf(stderr, "failed to parameter 2 is illegal");
        return -1;
    }

    /* get the sector size of the device */
    uint32_t sector_size;
    if (ioctl(fd, BLKSSZGET, &sector_size) < 0)
    {
        fprintf(stderr, "failed to get sector size (errno %d)\n", errno);
        close(fd);
        return 1;
    }
    uint32_t random_size = sector_size - sizeof(XXH32_hash_t);

    /* adjust sector size for storing the hash at the end */
    off_t device_size = lseek(fd, 0, SEEK_END);
    if (!device_size)
    {
        fprintf(stderr, "failed to get device size (errno %d)\n", errno);
        close(fd);
        return 1;
    }
    lseek(fd, 0, SEEK_SET);
    uint32_t total_sectors = device_size / sector_size;

    /* allocate buffer for reading and writing */
    uint32_t buffer_size = sector_size * write_sectors;
    uint8_t *buffer = malloc(buffer_size);
    if (!buffer)
    {
        fprintf(stderr, "failed to allocate buffer\n");
        return -1;
    }

    /* write phase */
    for (uint32_t i = 0; i < total_sectors / write_sectors; i++)
    {
        for (uint32_t j = 0; j < write_sectors; j++)
        {
            uint8_t *sector_data = buffer + (sector_size * j);

	    /* fill with random data */
            if (getrandom(sector_data, random_size, 0) < 0)
            {
                fprintf(stderr, "failed to get random data (errno %d)\n", errno);
                return -1;
            }

            /* calculate and store hash */
            XXH32_hash_t hash = XXH32(sector_data, random_size, 0);
            memcpy(sector_data + random_size, &hash, sizeof(hash));
        }

        /* write random data and hash to device */
        if (pwrite(fd, buffer, buffer_size, (__off_t)i * (__off_t)buffer_size) != buffer_size)
        {
            fprintf(stderr, "failed to write sector %" PRIu32 " (errno %d)\n", i, errno);
            break;
        }

        fprintf(stdout, "\rwrite progress: %.2f%% (%" PRIu32 "/%" PRIu32 ")", 100.0 * i / total_sectors, i, total_sectors);
        fflush(stdout);
    }
    fsync(fd);
    fprintf(stdout, "\n");

    /* verify phase */
    uint32_t valid_sectors = 0;
    for (uint32_t i = 0; i < total_sectors; i++)
    {
        /* read random data and hash from device */
        if (pread(fd, buffer, sector_size, (__off_t)i * (__off_t)sector_size) != sector_size)
        {
            fprintf(stderr, "failed to read sector %" PRIu32 " (errno %d)\n", i, errno);
            return -1;
        }

        /* calculate and extract hash */
        XXH32_hash_t hash;
        memcpy(&hash, buffer + random_size, sizeof(hash));

        /* compare hashes */
        if (hash == XXH32(buffer, random_size, 0))
        {
            valid_sectors++;
        }
        fprintf(stdout, "\rverify progress: %.2f%% (%" PRIu32 "/%" PRIu32 ")", 100.0 * i / total_sectors, i, total_sectors);
        fflush(stdout);
    }
    fprintf(stdout, "\n");

    close(fd);
    free(buffer);

    printf("sector size: %d bytes\n", sector_size);
    printf("total sectors: %" PRIu32 "\n", total_sectors);
    printf("valid sectors: %" PRIu32 "\n", valid_sectors);
    printf("estimated capacity: %" PRIu32 " bytes\n", valid_sectors * sector_size);
    return 0;
}
