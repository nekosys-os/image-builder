#include <iostream>
#include <getopt.h>
#include <fstream>
#include <cmath>

#include "file_content.h"
#include "buffer.h"

using namespace std;

void bad_args(char **argv) {
    std::cerr << "Usage: " << argv[0] << " -s <bootSectorPath> -l <bootLoaderPath> -k <kernelPath> -o <output>"
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
    memcpy(eightPoint3, name.c_str(), name.length());
    buf.write_at(offset, (const char *)eightPoint3);
    buf.write_at(offset + 11, attribute);
    buf.write_at(offset + 26, cluster);
    buf.write_at(offset + 28, filesize);

    std::cout << eightPoint3 << " as " << (int)attribute << " at " << offset << " (" << filesize << " B)" << std::endl;
}

int main(int argc, char *argv[]) {
    std::string bootSectorPath;
    std::string bootLoaderPath;
    std::string kernelPath;
    std::string outputPath;

    int opt;
    while ((opt = getopt(argc, argv, "s:l:k:o:")) != -1) {
        switch (opt) {
            case 's':
                bootSectorPath = std::string(optarg);
                break;
            case 'l':
                bootLoaderPath = std::string(optarg);
                break;
            case 'k':
                kernelPath = std::string(optarg);
                break;
            case 'o':
                outputPath = std::string(optarg);
                break;
            default:
                bad_args(argv);
        }
    }

    if (bootLoaderPath.empty() || bootLoaderPath.empty() || kernelPath.empty() || outputPath.empty()) {
        bad_args(argv);
    }

    log("Neko image builder v0.1.0");

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
    const int kernel_file_cluster = 3;
    const int cluster_size = bytes_per_block * blocks_per_alloc;

    buffer buf(image_size);

    // Write boot sector
    log("writing boot sector...");
    file_content bootSector = read_binary(bootSectorPath);
    buf.write_at(0, bootSector);

    // Write boot loader
    log("writing boot loader...");
    file_content bootLoader = read_binary(bootLoaderPath);
    buf.write_at(512, bootLoader);

    // Master Boot Record
    log("generating MBR...");
    buf.write_at<uint8_t>(0x1BE + 0x00, 0x80); // Bootable
    buf.write_at<uint8_t>(0x1BE + 0x04, 0x0E); // Disk type
    buf.write_at<int32_t>(0x1BE + 0x08, first_partition_block);
    buf.write_at<int32_t>(0x1BE + 0x0C, total_sectors);

    // FAT Partition
    log("generating FAT partition header");
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

    std::cout << "partition has " << total_sectors << " sectors" << std::endl;

    // Kernel
    log("generating FAT");
    file_content kernel = read_binary(kernelPath);

    // FAT table
    int fat0_idx = bytes_per_block * (first_partition_block + reserved_blocks);
    int fat1_idx = bytes_per_block * (first_partition_block + reserved_blocks + blocks_per_fat);

    int total_clusters = ceil((double) kernel.length / (double) cluster_size);
    std::cout << "kernel uses " << total_clusters << " clusters (" << kernel.length << "; " << cluster_size << ")"
              << std::endl;

    for (int i = 0; i < total_clusters; i++) {
        int currentClusterIdx = kernel_file_cluster + i;
        int currentClusterOffset = currentClusterIdx * 2;
        uint16_t next_cluster = (i < total_clusters - 1) ? (currentClusterIdx + 1) : 0xFFFF;
        std::cout << currentClusterIdx << " ref " << next_cluster << std::endl;

        buf.write_at(fat0_idx + currentClusterOffset, next_cluster);
        buf.write_at(fat1_idx + currentClusterOffset, next_cluster);
    }

    // Root directory
    log("generating rootdir");
    int root_dir_begin = bytes_per_block * (first_partition_block + reserved_blocks + blocks_per_fat * fat_count);
    std::cout << "root dir at " << root_dir_begin << std::endl;
    write_dirent(buf, root_dir_begin, "NEKOSYS", 0x08u, 0, 0);
    write_dirent(buf, root_dir_begin + 32, "NEKOKRNL", 0, kernel_file_cluster, kernel.length);
    int root_dir_end = root_dir_begin + root_dir_entries * 32;

    int kernel_file_offset = root_dir_end + cluster_size * (kernel_file_cluster - 2);
    std::cout << "writing kernel data to " << kernel_file_offset << std::endl;
    buf.write_at(kernel_file_offset, kernel);

    // Write output
    log("writing image file...");
    write_binary(outputPath, buf.to_file_content());

    // We're done
    log("Done");
    return 0;
}
