#ifndef QOI_FORMAT_CODEC_QOI_H_
#define QOI_FORMAT_CODEC_QOI_H_

#include "utils.h"

constexpr uint8_t QOI_OP_INDEX_TAG = 0x00;
constexpr uint8_t QOI_OP_DIFF_TAG  = 0x40;
constexpr uint8_t QOI_OP_LUMA_TAG  = 0x80;
constexpr uint8_t QOI_OP_RUN_TAG   = 0xc0; 
constexpr uint8_t QOI_OP_RGB_TAG   = 0xfe;
constexpr uint8_t QOI_OP_RGBA_TAG  = 0xff;
constexpr uint8_t QOI_PADDING[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
constexpr uint8_t QOI_MASK_2 = 0xc0;

/**
 * @brief encode the raw pixel data of an image to qoi format.
 *
 * @param[in] width image width in pixels
 * @param[in] height image height in pixels
 * @param[in] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[in] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace = 0);

/**
 * @brief decode the qoi format of an image to raw pixel data
 *
 * @param[out] width image width in pixels
 * @param[out] height image height in pixels
 * @param[out] channels number of color channels, 3 = RGB, 4 = RGBA
 * @param[out] colorspace image color space, 0 = sRGB with linear alpha, 1 = all channels linear
 *
 * @return bool true if it is a valid qoi format image, false otherwise
 */
bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace);


bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace) {

    // qoi-header part

    // write magic bytes "qoif"
    QoiWriteChar('q');
    QoiWriteChar('o');
    QoiWriteChar('i');
    QoiWriteChar('f');
    // write image width
    QoiWriteU32(width);
    // write image height
    QoiWriteU32(height);
    // write channel number
    QoiWriteU8(channels);
    // write color space specifier
    QoiWriteU8(colorspace);

    /* qoi-data part */
    int run = 0;
    int px_num = width * height;

    uint8_t history[64][4];
    memset(history, 0, sizeof(history));

    uint8_t r, g, b, a;
    a = 255u;
    uint8_t pre_r, pre_g, pre_b, pre_a;
    pre_r = 0u;
    pre_g = 0u;
    pre_b = 0u;
    pre_a = 255u;

    for (int i = 0; i < px_num; ++i) {
        r = QoiReadU8();
        g = QoiReadU8();
        b = QoiReadU8();
        if (channels == 4) a = QoiReadU8();

        // Check if current pixel is the same as previous (for QOI_OP_RUN)
        if (r == pre_r && g == pre_g && b == pre_b && a == pre_a) {
            run++;
            if (run == 62 || i == px_num - 1) {
                // QOI_OP_RUN: 2-bit tag (11) + 6-bit run length
                QoiWriteU8(QOI_OP_RUN_TAG | (run - 1));
                run = 0;
            }
        } else {
            // Flush any pending run
            if (run > 0) {
                QoiWriteU8(QOI_OP_RUN_TAG | (run - 1));
                run = 0;
            }
            
            // Update history array first
            int index = QoiColorHash(r, g, b, a);
            history[index][0] = r;
            history[index][1] = g;
            history[index][2] = b;
            history[index][3] = a;
            
            // Check QOI_OP_INDEX
            if (history[index][0] == r && history[index][1] == g && 
                history[index][2] == b && history[index][3] == a) {
                // QOI_OP_INDEX: 2-bit tag (00) + 6-bit index
                QoiWriteU8(index);
            } else {
                
                // Check QOI_OP_DIFF (small differences, alpha unchanged)
                if (a == pre_a &&
                     (int8_t)(r - pre_r) >= -2 && (int8_t)(r - pre_r) <= 1 &&
                     (int8_t)(g - pre_g) >= -2 && (int8_t)(g - pre_g) <= 1 &&
                     (int8_t)(b - pre_b) >= -2 && (int8_t)(b - pre_b) <= 1) {
                    // QOI_OP_DIFF: 2-bit tag (01) + 2-bit red diff + 2-bit green diff + 2-bit blue diff
                    uint8_t diff_r = (r - pre_r) + 2;
                    uint8_t diff_g = (g - pre_g) + 2;
                    uint8_t diff_b = (b - pre_b) + 2;
                    QoiWriteU8(QOI_OP_DIFF_TAG | (diff_r << 4) | (diff_g << 2) | diff_b);
                }
                // Check QOI_OP_LUMA (larger differences)
                else if (a == pre_a &&
                         (int8_t)(g - pre_g) >= -32 && (int8_t)(g - pre_g) <= 31 &&
                         (int8_t)(r - pre_r) >= -8 && (int8_t)(r - pre_r) <= 7 &&
                         (int8_t)(b - pre_b) >= -8 && (int8_t)(b - pre_b) <= 7) {
                    // QOI_OP_LUMA: 2-bit tag (10) + 6-bit green diff + 4-bit red diff + 4-bit blue diff
                    int8_t diff_g = g - pre_g;
                    int8_t diff_r = r - pre_r - diff_g;
                    int8_t diff_b = b - pre_b - diff_g;
                    uint8_t luma_g = diff_g + 32;
                    uint8_t luma_rb = ((diff_r + 8) << 4) | (diff_b + 8);
                    QoiWriteU8(QOI_OP_LUMA_TAG | luma_g);
                    QoiWriteU8(luma_rb);
                }
                // Check QOI_OP_RGB (full RGB, alpha unchanged)
                else if (a == pre_a) {
                    // QOI_OP_RGB: 8-bit tag (11111110) + 8-bit red + 8-bit green + 8-bit blue
                    QoiWriteU8(QOI_OP_RGB_TAG);
                    QoiWriteU8(r);
                    QoiWriteU8(g);
                    QoiWriteU8(b);
                }
                // QOI_OP_RGBA (full RGBA)
                else {
                    // QOI_OP_RGBA: 8-bit tag (11111111) + 8-bit red + 8-bit green + 8-bit blue + 8-bit alpha
                    QoiWriteU8(QOI_OP_RGBA_TAG);
                    QoiWriteU8(r);
                    QoiWriteU8(g);
                    QoiWriteU8(b);
                    QoiWriteU8(a);
                }
            }
        }

        pre_r = r;
        pre_g = g;
        pre_b = b;
        pre_a = a;
    }

    // qoi-padding part
    for (int i = 0; i < sizeof(QOI_PADDING) / sizeof(QOI_PADDING[0]); ++i) {
        QoiWriteU8(QOI_PADDING[i]);
    }

    return true;
}

bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace) {

    char c1 = QoiReadChar();
    char c2 = QoiReadChar();
    char c3 = QoiReadChar();
    char c4 = QoiReadChar();
    if (c1 != 'q' || c2 != 'o' || c3 != 'i' || c4 != 'f') {
        return false;
    }

    // read image width
    width = QoiReadU32();
    // read image height
    height = QoiReadU32();
    // read channel number
    channels = QoiReadU8();
    // read color space specifier
    colorspace = QoiReadU8();

    int run = 0;
    int px_num = width * height;

    uint8_t history[64][4];
    memset(history, 0, sizeof(history));

    uint8_t r, g, b, a;
    a = 255u;
    uint8_t pre_r, pre_g, pre_b, pre_a;
    pre_r = 0u;
    pre_g = 0u;
    pre_b = 0u;
    pre_a = 255u;

    for (int i = 0; i < px_num; ++i) {
        if (run > 0) {
            run--;
        } else {
            uint8_t op = QoiReadU8();
            uint8_t tag = op & QOI_MASK_2;
            
            if (tag == QOI_OP_INDEX_TAG) {
                // QOI_OP_INDEX: 2-bit tag (00) + 6-bit index
                uint8_t index = op & 0x3f;
                r = history[index][0];
                g = history[index][1];
                b = history[index][2];
                a = history[index][3];
            }
            else if (tag == QOI_OP_DIFF_TAG) {
                // QOI_OP_DIFF: 2-bit tag (01) + 2-bit red diff + 2-bit green diff + 2-bit blue diff
                uint8_t diff_r = (op >> 4) & 0x03;
                uint8_t diff_g = (op >> 2) & 0x03;
                uint8_t diff_b = op & 0x03;
                r = pre_r + diff_r - 2;
                g = pre_g + diff_g - 2;
                b = pre_b + diff_b - 2;
                // alpha unchanged
            }
            else if (tag == QOI_OP_LUMA_TAG) {
                // QOI_OP_LUMA: 2-bit tag (10) + 6-bit green diff + 4-bit red diff + 4-bit blue diff
                uint8_t luma_g = op & 0x3f;
                uint8_t luma_rb = QoiReadU8();
                int8_t diff_g = luma_g - 32;
                int8_t diff_r = ((luma_rb >> 4) & 0x0f) - 8;
                int8_t diff_b = (luma_rb & 0x0f) - 8;
                r = pre_r + diff_r + diff_g;
                g = pre_g + diff_g;
                b = pre_b + diff_b + diff_g;
                // alpha unchanged
            }
            else if (tag == QOI_OP_RUN_TAG) {
                // QOI_OP_RUN: 2-bit tag (11) + 6-bit run length
                run = (op & 0x3f);
                // Current pixel is the same as previous
                r = pre_r;
                g = pre_g;
                b = pre_b;
                a = pre_a;
                // Don't decrement run here - it will be decremented at the start of the loop
            }
            else if (op == QOI_OP_RGB_TAG) {
                // QOI_OP_RGB: 8-bit tag (11111110) + 8-bit red + 8-bit green + 8-bit blue
                r = QoiReadU8();
                g = QoiReadU8();
                b = QoiReadU8();
                // alpha unchanged
            }
            else if (op == QOI_OP_RGBA_TAG) {
                // QOI_OP_RGBA: 8-bit tag (11111111) + 8-bit red + 8-bit green + 8-bit blue + 8-bit alpha
                r = QoiReadU8();
                g = QoiReadU8();
                b = QoiReadU8();
                a = QoiReadU8();
            }
        }
        
        QoiWriteU8(r);
        QoiWriteU8(g);
        QoiWriteU8(b);
        if (channels == 4) QoiWriteU8(a);
        
        // Update history array after writing output
        int index = QoiColorHash(r, g, b, a);
        history[index][0] = r;
        history[index][1] = g;
        history[index][2] = b;
        history[index][3] = a;
        
        pre_r = r;
        pre_g = g;
        pre_b = b;
        pre_a = a;
    }

    bool valid = true;
    for (int i = 0; i < sizeof(QOI_PADDING) / sizeof(QOI_PADDING[0]); ++i) {
        if (QoiReadU8() != QOI_PADDING[i]) valid = false;
    }

    return valid;
}

#endif // QOI_FORMAT_CODEC_QOI_H_
