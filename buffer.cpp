//
// Created by tim on 12/8/20.
//

#include <cstring>
#include "buffer.h"

buffer::buffer(int dataSize) : dataSize(dataSize), data(new uint8_t[dataSize]) {}

file_content buffer::to_file_content() {
    return {dataSize, data};
}

void buffer::write_at(int offset, file_content &content) {
    memcpy(data + offset, content.data, content.length);
}

buffer::~buffer() {
    delete[] data;
}

void buffer::write_at(int offset, const char *str) {
    memcpy(data + offset, str, strlen(str));
}
