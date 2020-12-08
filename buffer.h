//
// Created by tim on 12/8/20.
//

#ifndef IMAGE_BUILDER_BUFFER_H
#define IMAGE_BUILDER_BUFFER_H

#include <cstdint>
#include <cstring>
#include "file_content.h"

class buffer {
private:
    int dataSize;
    uint8_t *data;

public:
    explicit buffer(int dataSize);

    ~buffer();

    void write_at(int offset, file_content &content);

    void write_at(int offset, const char *str);

    template<typename data>
    void write_at(int offset, data d) {
        memcpy(this->data + offset, &d, sizeof(d));
    }

    file_content to_file_content();
};


#endif //IMAGE_BUILDER_BUFFER_H
