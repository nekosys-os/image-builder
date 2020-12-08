#include <iostream>
#include <getopt.h>
#include <fstream>

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
    std::cout << "[nkimg] " << msg << std::endl;
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

    const int partition_size = 16 * 1024 * 1024;
    const int first_partition_block = 128;
    const int bytes_per_block = 512;
    const int blocks_per_alloc = 8;
    const int reserved_blocks = 8;
    const int fat_count = 2;
    const int root_dir_entries = 512;
    const int media_descriptor_type = 0xFF;
    const int blocks_per_fat = 256;
    const int hidden_blocks = 128;
    const int total_sectors = partition_size / 512 - first_partition_block;

    buffer buf(partition_size);

    // Write boot sector
    log("Writing boot sector...");
    file_content bootSector = read_binary(bootSectorPath);
    buf.write_at(0, bootSector);

    // Write boot loader
    log("Writing bootloader...");
    file_content bootLoader = read_binary(bootLoaderPath);
    buf.write_at(512, bootLoader);

    // Master Boot Record
    log("Generating MBR...");
    buf.write_at<uint8_t>(0x1BE + 0x00, 0x80); // Bootable
    buf.write_at<uint8_t>(0x1BE + 0x04, 0x0E); // Disk type
    buf.write_at<int32_t>(0x1BE + 0x08, first_partition_block);
    buf.write_at<int32_t>(0x1BE + 0x0C, total_sectors);

    // FAT Partition
    log("Generating FAT FS...");
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

    // FAT table
    int fat0_idx = bytes_per_block * (first_partition_block + reserved_blocks);
    int fat1_idx = bytes_per_block * (first_partition_block + reserved_blocks + blocks_per_fat);
    int root_dir_begin = bytes_per_block * (first_partition_block + reserved_blocks + blocks_per_fat * fat_count);
    buf.write_at<uint16_t>(fat0_idx, 0xFFFF);
    buf.write_at<uint16_t>(fat1_idx, 0xFFFF);

    // Write output
    log("Writing image file...");
    write_binary(outputPath, buf.to_file_content());

    // We're done
    log("Done");
    return 0;
}
