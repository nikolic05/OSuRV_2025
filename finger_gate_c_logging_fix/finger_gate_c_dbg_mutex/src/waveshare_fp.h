#pragma once
#include <stdint.h>

typedef struct {
    int fd;
    uint8_t sid;
    uint8_t did;
} fp_dev_t;

int fp_init(fp_dev_t* dev, int fd, uint8_t sid, uint8_t did);

int fp_finger_detect(fp_dev_t* dev, int* present);
int fp_get_image(fp_dev_t* dev);
int fp_generate(fp_dev_t* dev, uint16_t rambuf_id);
int fp_search(fp_dev_t* dev, uint16_t rambuf_id, uint16_t start_id, uint16_t end_id, int* matched_id);

int fp_merge(fp_dev_t* dev, uint16_t out_rambuf_id, uint8_t count);
int fp_store(fp_dev_t* dev, uint16_t template_id, uint16_t rambuf_id, uint16_t* duplication_id_opt);

int fp_get_empty_id(fp_dev_t* dev, uint16_t start_id, uint16_t end_id, uint16_t* out_id);
