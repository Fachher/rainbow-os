#include "include/pmm.h"
#include "lib/string.h"
#include "drivers/serial.h"

/* Bitmap: 1 bit per 4K frame. 32MB / 4K = 8192 frames = 1024 bytes = 256 uint32_t */
#define BITMAP_SIZE (TOTAL_FRAMES / 32)

static uint32_t frame_bitmap[BITMAP_SIZE];
static uint32_t used_frames;

static inline void bitmap_set(uint32_t frame) {
    frame_bitmap[frame / 32] |= (1 << (frame % 32));
}

static inline void bitmap_clear(uint32_t frame) {
    frame_bitmap[frame / 32] &= ~(1 << (frame % 32));
}

static inline bool bitmap_test(uint32_t frame) {
    return frame_bitmap[frame / 32] & (1 << (frame % 32));
}

void pmm_init(uint32_t kernel_end) {
    /* Mark all frames as used initially */
    memset(frame_bitmap, 0xFF, sizeof(frame_bitmap));
    used_frames = TOTAL_FRAMES;

    /* Free usable frames: from after kernel to end of RAM */
    uint32_t first_free = (kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t first_frame = first_free / PAGE_SIZE;

    for (uint32_t i = first_frame; i < TOTAL_FRAMES; i++) {
        bitmap_clear(i);
        used_frames--;
    }

    serial_write("PMM: ");
    serial_write_dec(pmm_free_count());
    serial_write(" free frames (");
    serial_write_dec(pmm_free_count() * 4);
    serial_write(" KB), kernel ends at ");
    serial_write_hex(kernel_end);
    serial_write("\n");
}

uint32_t pmm_alloc_frame(void) {
    for (uint32_t i = 0; i < BITMAP_SIZE; i++) {
        if (frame_bitmap[i] == 0xFFFFFFFF) continue;

        for (uint32_t bit = 0; bit < 32; bit++) {
            if (!(frame_bitmap[i] & (1 << bit))) {
                uint32_t frame = i * 32 + bit;
                bitmap_set(frame);
                used_frames++;
                return frame * PAGE_SIZE;
            }
        }
    }
    /* Out of memory */
    serial_write("PMM: OUT OF MEMORY!\n");
    return 0;
}

void pmm_free_frame(uint32_t phys_addr) {
    uint32_t frame = phys_addr / PAGE_SIZE;
    if (!bitmap_test(frame)) return;
    bitmap_clear(frame);
    used_frames--;
}

uint32_t pmm_free_count(void) {
    return TOTAL_FRAMES - used_frames;
}

uint32_t pmm_used_count(void) {
    return used_frames;
}
