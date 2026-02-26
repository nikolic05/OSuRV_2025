#include "waveshare_fp.h"
#include "uart.h"
#include <string.h>
#include <stdio.h>

#define PKT_LEN 26

#define CMD_PREFIX 0xAA55
#define RSP_PREFIX 0x55AA

#define CMD_TEST_CONNECTION 0x0001
#define CMD_GET_IMAGE       0x0020
#define CMD_FINGER_DETECT   0x0021
#define CMD_GENERATE        0x0060
#define CMD_MERGE           0x0061
#define CMD_MATCH           0x0062
#define CMD_SEARCH          0x0063
#define CMD_STORE_CHAR           0x0040
#define CMD_GET_EMPTY_ID    0x0045


#define ERR_SUCCESS         0x0000

//citanje dva bajta
static uint16_t le16(const uint8_t* p) { 
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8); 
}
//upis 2 bajta
static void wr16le(uint8_t* p, uint16_t v) { 
    p[0] = (uint8_t)(v & 0xFF); 
    p[1] = (uint8_t)(v >> 8); 
}

static uint16_t checksum16(const uint8_t* p24) {
    uint32_t s = 0;
    for (int i = 0; i < 24; i++) s += p24[i];
    return (uint16_t)(s & 0xFFFF);
}
//pravljenje paketa + slanje + primanje
static int send_cmd(fp_dev_t* dev, uint16_t cmd, const uint8_t* data, uint16_t data_len, uint8_t rsp[PKT_LEN]) {
    //paket
    uint8_t pkt[PKT_LEN];
    memset(pkt, 0, sizeof pkt);

    wr16le(pkt + 0, CMD_PREFIX);
    pkt[2] = dev->sid;
    pkt[3] = dev->did;
    wr16le(pkt + 4, cmd);
    wr16le(pkt + 6, data_len);

    if (data_len > 16) return -10;
    if (data && data_len) memcpy(pkt + 8, data, data_len);

    uint16_t cks = checksum16(pkt);
    wr16le(pkt + 24, cks);

    //slanje paketa na uart
    if (uart_write_all(dev->fd, pkt, PKT_LEN) != 0) return -1;
    //citanje odgovora
    return uart_read_all(dev->fd, rsp, PKT_LEN, 1000);
}

//parsiaranje odgovora
static int parse_rsp(const uint8_t rsp[PKT_LEN], uint16_t* rcm, uint16_t* ret, uint8_t data14[14]) {
    uint16_t prefix = le16(rsp + 0);
    if (prefix != RSP_PREFIX) return -20;

    uint16_t calc = checksum16(rsp);//izracunat checksum
    uint16_t got  = le16(rsp + 24);//primljen checksum
    if (calc != got) return -21;

    *rcm = le16(rsp + 4);
    *ret = le16(rsp + 8);
    memcpy(data14, rsp + 10, 14);
    return 0;
}

//inicijalizacija + ping
int fp_init(fp_dev_t* dev, int fd, uint8_t sid, uint8_t did) {
    dev->fd = fd;
    dev->sid = sid;
    dev->did = did;

    uint8_t rsp[PKT_LEN];
    uint16_t rcm, ret;
    uint8_t d[14];

    int r = send_cmd(dev, CMD_TEST_CONNECTION, NULL, 0, rsp);//ping
    if (r != 0) return r;
    r = parse_rsp(rsp, &rcm, &ret, d);
    if (r != 0) return r;
    if (rcm != CMD_TEST_CONNECTION || ret != ERR_SUCCESS) return -30;
    return 0;
}
//detekcija prsta
int fp_finger_detect(fp_dev_t* dev, int* present) {
    uint8_t rsp[PKT_LEN];
    uint16_t rcm, ret;
    uint8_t d[14];

    int r = send_cmd(dev, CMD_FINGER_DETECT, NULL, 0, rsp);
    if (r != 0) return r;
    r = parse_rsp(rsp, &rcm, &ret, d);
    if (r != 0) return r;
    if (rcm != CMD_FINGER_DETECT) return -40;

    
    *present = (ret == ERR_SUCCESS && d[0] == 0x01) ? 1 : 0;
    return 0;
}

//skeniranje prsta
int fp_get_image(fp_dev_t* dev) {
    uint8_t rsp[PKT_LEN];
    uint16_t rcm, ret;
    uint8_t d[14];
    int r = send_cmd(dev, CMD_GET_IMAGE, NULL, 0, rsp);
    if (r != 0) return r;
    r = parse_rsp(rsp, &rcm, &ret, d);
    if (r != 0) return r;
    if (rcm != CMD_GET_IMAGE) return -41;
    return (ret == ERR_SUCCESS) ? 0 : -42;
}

//generisanje template-a
int fp_generate(fp_dev_t* dev, uint16_t rambuf_id) {
    uint8_t rsp[PKT_LEN];
    uint16_t rcm, ret;
    uint8_t d[14];

    uint8_t data[2];
    wr16le(data, rambuf_id);

    int r = send_cmd(dev, CMD_GENERATE, data, 2, rsp);
    if (r != 0) return r;
    r = parse_rsp(rsp, &rcm, &ret, d);
    if (r != 0) return r;
    if (rcm != CMD_GENERATE) return -43;
    return (ret == ERR_SUCCESS) ? 0 : -44;
}
//pretrazivanje u bazi
int fp_search(fp_dev_t* dev, uint16_t rambuf_id, uint16_t start_id, uint16_t end_id, int* matched_id) {
    uint8_t rsp[PKT_LEN];
    uint16_t rcm, ret;
    uint8_t d[14];

    uint8_t data[6];
    wr16le(data + 0, rambuf_id);
    wr16le(data + 2, start_id);
    wr16le(data + 4, end_id);

    int r = send_cmd(dev, CMD_SEARCH, data, 6, rsp);
    if (r != 0) return r;
    r = parse_rsp(rsp, &rcm, &ret, d);
    if (r != 0) return r;
    if (rcm != CMD_SEARCH) return -45;

    if (ret != ERR_SUCCESS) return 1; // no match
    *matched_id = (int)le16(d + 0);//id u bazi koji se poklapa
    return 0;
}
//spajanje uzoraka
int fp_merge(fp_dev_t* dev, uint16_t out_rambuf_id, uint8_t count) {
    uint8_t rsp[PKT_LEN];
    uint16_t rcm, ret;
    uint8_t d[14];

    uint8_t data[3];

    //data = 2 bajta RamBuffer ID + 1 bajt Count(broj uzoraka)
    wr16le(data + 0, out_rambuf_id);
    data[2] = count;

    int r = send_cmd(dev, CMD_MERGE, data, 3, rsp);
    if (r != 0) return r;
    r = parse_rsp(rsp, &rcm, &ret, d);
    if (r != 0) return r;
    if (rcm != CMD_MERGE) return -46;
    return (ret == ERR_SUCCESS) ? 0 : -(int)ret; // vracamo negativan modulski error code
}

//upis u bazu
int fp_store(fp_dev_t* dev, uint16_t template_id, uint16_t rambuf_id, uint16_t* duplication_id_opt) {
    uint8_t rsp[PKT_LEN];
    uint16_t rcm, ret;
    uint8_t d[14];

    //data = 2 bajta Template ID + 2 bajta RamBufferID
    uint8_t data[4];
    wr16le(data + 0, template_id);
    wr16le(data + 2, rambuf_id);

    int r = send_cmd(dev, CMD_STORE_CHAR, data, 4, rsp);
    if (r != 0) return r;
    r = parse_rsp(rsp, &rcm, &ret, d);
    if (r != 0) return r;
    if (rcm != CMD_STORE_CHAR) return -48;

    if (duplication_id_opt) *duplication_id_opt = le16(d + 0);

    return (ret == ERR_SUCCESS) ? 0 : -(int)ret;
}

//prvi slobodan id
int fp_get_empty_id(fp_dev_t* dev, uint16_t start_id, uint16_t end_id, uint16_t* out_id) {
    uint8_t rsp[PKT_LEN];
    uint16_t rcm, ret;
    uint8_t d[14];

    uint8_t data[4];
    wr16le(data + 0, start_id);
    wr16le(data + 2, end_id);

    int r = send_cmd(dev, CMD_GET_EMPTY_ID, data, 4, rsp);
    if (r != 0) return r;
    r = parse_rsp(rsp, &rcm, &ret, d);
    if (r != 0) return r;
    if (rcm != CMD_GET_EMPTY_ID) return -50;

    if (ret != ERR_SUCCESS) return -(int)ret;
    
    if (out_id) *out_id = le16(d + 0);//prva dva bajta payload-a su ID slobodnog slota
    return 0;
}
