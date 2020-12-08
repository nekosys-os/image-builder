//
// Created by tim on 12/8/20.
//

#ifndef IMAGE_BUILDER_FILE_CONTENT_H
#define IMAGE_BUILDER_FILE_CONTENT_H

#include <cstdint>

struct file_content {
    int length;
    uint8_t *data;
};

#endif //IMAGE_BUILDER_FILE_CONTENT_H
