#include <iostream>
#include <getopt.h>
#include <fstream>
#include <cmath>
#include <filesystem>

#include "file_content.h"
#include "buffer.h"

#define ATTR_READONLY 0x01u
#define ATTR_HIDDEN   0x02u
#define ATTR_SYSTEM   0x04u
#define ATTR_VOLUME   0x08u
#define ATTR_SUBDIR   0x10u
#define ATTR_ARCHIVE  0x20u
#define ATTR_DEVICE   0x40u

using namespace std;

void bad_args(char **argv) {
    std::cerr << "Usage: " << argv[0] << " -s <bootSectorPath> -l <bootLoaderPath> -r <fileSystemRoot> -o <output>"
              << std::endl;
    exit(1);
}

file_content read_binary(const std::string &path) {
    ifstream stream(path, ios::binary);

    stream.seekg(0, ios::end);
    int length = stream.tellg();
    stream.seekg(0, ios::beg);

    auto *data = new uint8_t[length];
    stream.read(reinterpret_cast<char *>(data), length);
    stream.close();

    return {length, data};
}

void write_binary(const std::string &path, const file_content &content) {
    ofstream stream(path, ios::binary);
    stream.write(reinterpret_cast<const char *>(content.data), content.length);
    stream.close();
}

void log(const std::string &msg) {
    std::cout << msg << std::endl;
}

void
write_dirent(buffer &buf, int offset, const std::string &name, uint8_t attribute, uint16_t cluster, uint32_t filesize) {
    char eightPoint3[12];
    memset(eightPoint3, 0, 12);

    memcpy(eightPoint3, name.c_str(), name.length());
    buf.write_at(offset, (const char *) eightPoint3);
    buf.write_at(offset + 11, attribute);
    buf.write_at(offset + 26, cluster);
    buf.write_at(offset + 28, filesize);

    std::cout << "DIRENT: " << eightPoint3 << " as " << (int) attribute << " at " << offset << " (" << filesize << " B)"
              << std::endl;
}

void
write_dir(buffer &buf, int base, const std::string &path, int *cluster, int fat0_idx, int fat1_idx, int cluster_size,
          int root_dir_end) {
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        if (entry.path().filename().string().length() > 11) {
            std::cout << "SKIPPED: " << entry.path().filename().string() << " because its name exceeds 8.3"
                      << std::endl;
            continue;
        }

        if (entry.is_directory()) {
            std::cout << "BEGIN SUBDIR: " << entry.path().filename().string() << std::endl;
            write_dirent(buf, base, entry.path().filename().string(), ATTR_SUBDIR, *cluster, 0);
            base += 32;

            int subdir_offset = root_dir_end + cluster_size * (*cluster - 2);
            std::cout << " Writing subdir to " << *cluster << " / " << subdir_offset << std::endl;

            buf.write_at(fat0_idx + (2 * *cluster), 0xFFFF);
            buf.write_at(fat1_idx + (2 * *cluster), 0xFFFF);
            *cluster += 1;

            write_dir(buf, subdir_offset, path + "/" + entry.path().filename().string(), cluster, fat0_idx, fat1_idx,
                      cluster_size, root_dir_end);
            std::cout << "END SUBDIR: " << entry.path().filename().string() << std::endl;
            continue;
        }

        auto file = read_binary(entry.path().string());

        int total_clusters = ceil((double) file.length / (double) cluster_size);
        std::cout << "FILE: " << entry.path().filename().string() << " (" << total_clusters << " clusters, "
                  << file.length << " B) at " << *cluster << std::endl;

        // write FAT
        for (int i = 0; i < total_clusters; i++) {
            int currentClusterIdx = *cluster + i;
            int currentClusterOffset = currentClusterIdx * 2;
            uint16_t next_cluster = (i < total_clusters - 1) ? (currentClusterIdx + 1) : 0xFFFF;
            std::cout << "  " << currentClusterIdx << " -> " << next_cluster << std::endl;

            buf.write_at(fat0_idx + currentClusterOffset, next_cluster);
            buf.write_at(fat1_idx + currentClusterOffset, next_cluster);
        }

        // write dirent
        write_dirent(buf, base, entry.path().filename().string(), 0, *cluster, file.length);

        // write actual file data
        int file_offset = root_dir_end + cluster_size * (*cluster - 2);
        buf.write_at(file_offset, file);

        base += 32;
        *cluster += total_clusters;
        std::cout << std::endl;
    }
}

int main(int argc, char *argv[]) {
    std::string bootSectorPath;
    std::string bootLoaderPath;
    std::string fsRootPath;
    std::string outputPath;

    int opt;
    while ((opt = getopt(argc, argv, "s:l:r:o:")) != -1) {
        switch (opt) {
            case 's':
                bootSectorPath = std::string(optarg);
                break;
            case 'l':
                bootLoaderPath = std::string(optarg);
                break;
            case 'r':
                fsRootPath = std::string(optarg);
                break;
            case 'o':
                outputPath = std::string(optarg);
                break;
            default:
                bad_args(argv);
        }
    }

    if (bootLoaderPath.empty() || bootLoaderPath.empty() || fsRootPath.empty() || outputPath.empty()) {
        bad_args(argv);
    }

    log("** Neko image builder v0.3.0 **");

    const int image_size = 16 * 1024 * 1024;
    const int first_partition_block = 128;
    const int bytes_per_block = 512;
    const int blocks_per_alloc = 8;
    const int reserved_blocks = 8;
    const int fat_count = 2;
    const int root_dir_entries = 512;
    const int media_descriptor_type = 0xFF;
    const int blocks_per_fat = 256;
    const int hidden_blocks = 128;
    const int total_sectors = image_size / 512 - first_partition_block;
    const int cluster_size = bytes_per_block * blocks_per_alloc;

    buffer buf(image_size);

    // Write boot sector
    log("WRITE: Boot sector");
    file_content bootSector = read_binary(bootSectorPath);
    buf.write_at(0, bootSector);

    // Write boot loader
    log("WRITE: Boot loader");
    file_content bootLoader = read_binary(bootLoaderPath);
    buf.write_at(512, bootLoader);

    // Master Boot Record
    log("WRITE: Master Boot Record");
    buf.write_at<uint8_t>(0x1BE + 0x00, 0x80); // Bootable
    buf.write_at<uint8_t>(0x1BE + 0x04, 0x0E); // Disk type
    buf.write_at<int32_t>(0x1BE + 0x08, first_partition_block);
    buf.write_at<int32_t>(0x1BE + 0x0C, total_sectors);

    // FAT Partition
    log("WRITE: FAT partition header");
    int first_partition_byte_offset = bytes_per_block * first_partition_block;
    buf.write_at(first_partition_byte_offset + 0x03, "MSWIN4.1");
    buf.write_at<int16_t>(first_partition_byte_offset + 0x0b, bytes_per_block);
    buf.write_at<uint8_t>(first_partition_byte_offset + 0x0d, blocks_per_alloc);
    buf.write_at<int16_t>(first_partition_byte_offset + 0x0e, reserved_blocks);
    buf.write_at<uint8_t>(first_partition_byte_offset + 0x10, fat_count);
    buf.write_at<int16_t>(first_partition_byte_offset + 0x11, root_dir_entries);
    buf.write_at<uint8_t>(first_partition_byte_offset + 0x15, media_descriptor_type);
    buf.write_at<int16_t>(first_partition_byte_offset + 0x16, blocks_per_fat);
    buf.write_at<int16_t>(first_partition_byte_offset + 0x1c, hidden_blocks);
    buf.write_at<int16_t>(first_partition_byte_offset + 0x20, total_sectors);

    std::cout << "  partition has " << total_sectors << " sectors" << std::endl;

    // Write directory
    int root_dir_begin = bytes_per_block * (first_partition_block + reserved_blocks + blocks_per_fat * fat_count);
    std::cout << "ROOTDIR: At " << root_dir_begin << std::endl;

    write_dirent(buf, root_dir_begin, "NEKOSYS", ATTR_VOLUME, 0, 0);
    int root_dir_end = root_dir_begin + root_dir_entries * 32;
    int next_root_dir_ent = root_dir_begin + 32;

    std::cout << std::endl;

    // Write data
    int fat0_idx = bytes_per_block * (first_partition_block + reserved_blocks);
    int fat1_idx = bytes_per_block * (first_partition_block + reserved_blocks + blocks_per_fat);

    int cluster = 3;

    write_dir(buf, next_root_dir_ent, fsRootPath, &cluster, fat0_idx, fat1_idx, cluster_size, root_dir_end);

    // Write output
    log("OUTPUT: Writing image file...");
    write_binary(outputPath, buf.to_file_content());

    // We're done
    log("Done");
    return 0;
}
