#include "fat12.h"
#include "lib/string.h"

#define SECTOR_SIZE 512

/* BPB fields we care about */
static uint8_t  *vol;
static uint16_t bytes_per_sector;
static uint8_t  sectors_per_cluster;
static uint16_t reserved_sectors;
static uint8_t  num_fats;
static uint16_t root_entry_count;
static uint16_t total_sectors;
static uint16_t sectors_per_fat;

/* Derived */
static uint32_t root_dir_offset;
static uint32_t root_dir_sectors;
static uint32_t data_start_offset;

static uint16_t read16(uint8_t *p) {
    return p[0] | (p[1] << 8);
}

/* On-disk directory entry (32 bytes) */
struct fat12_raw_dirent {
    uint8_t  name[11];      /* 8.3 format, space padded */
    uint8_t  attr;
    uint8_t  reserved[10];
    uint16_t time;
    uint16_t date;
    uint16_t first_cluster;
    uint32_t size;
} __attribute__((packed));

/* Read a 12-bit FAT entry */
static uint16_t fat12_read_fat(uint16_t cluster) {
    uint32_t fat_offset = reserved_sectors * bytes_per_sector;
    uint32_t entry_offset = cluster + (cluster / 2);  /* 1.5 bytes per entry */
    uint8_t *p = vol + fat_offset + entry_offset;
    uint16_t val = read16(p);

    if (cluster & 1) {
        return val >> 4;
    } else {
        return val & 0x0FFF;
    }
}

/* Convert cluster number to byte offset in volume */
static uint32_t cluster_to_offset(uint16_t cluster) {
    return data_start_offset + (cluster - 2) * sectors_per_cluster * bytes_per_sector;
}

/* Convert 8.3 name to friendly "name.ext" format */
static void format_name(const uint8_t *raw, char *out) {
    int i, j = 0;

    /* Copy name part, trim trailing spaces */
    for (i = 0; i < 8 && raw[i] != ' '; i++) {
        /* Lowercase for display */
        out[j++] = (raw[i] >= 'A' && raw[i] <= 'Z') ? raw[i] + 32 : raw[i];
    }

    /* Extension */
    if (raw[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && raw[i] != ' '; i++) {
            out[j++] = (raw[i] >= 'A' && raw[i] <= 'Z') ? raw[i] + 32 : raw[i];
        }
    }

    out[j] = '\0';
}

/* Convert friendly name to 8.3 uppercase padded format for comparison */
static void name_to_83(const char *name, uint8_t *out) {
    int i;
    memset(out, ' ', 11);

    /* Find dot */
    int dot = -1;
    for (i = 0; name[i]; i++) {
        if (name[i] == '.') { dot = i; break; }
    }

    /* Name part */
    int limit = dot >= 0 ? dot : (int)strlen(name);
    if (limit > 8) limit = 8;
    for (i = 0; i < limit; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[i] = c;
    }

    /* Extension */
    if (dot >= 0) {
        const char *ext = name + dot + 1;
        for (i = 0; i < 3 && ext[i]; i++) {
            char c = ext[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[8 + i] = c;
        }
    }
}

void fat12_init(uint8_t *volume, uint32_t volume_size) {
    (void)volume_size;
    vol = volume;

    bytes_per_sector    = read16(vol + 11);
    sectors_per_cluster = vol[13];
    reserved_sectors    = read16(vol + 14);
    num_fats            = vol[16];
    root_entry_count    = read16(vol + 17);
    total_sectors       = read16(vol + 19);
    sectors_per_fat     = read16(vol + 22);

    root_dir_offset  = (reserved_sectors + num_fats * sectors_per_fat) * bytes_per_sector;
    root_dir_sectors = ((root_entry_count * 32) + bytes_per_sector - 1) / bytes_per_sector;
    data_start_offset = root_dir_offset + root_dir_sectors * bytes_per_sector;
}

int fat12_list_root(struct fat12_dirent *buf, int max_entries) {
    int count = 0;
    struct fat12_raw_dirent *dir = (struct fat12_raw_dirent *)(vol + root_dir_offset);

    for (uint16_t i = 0; i < root_entry_count && count < max_entries; i++) {
        if (dir[i].name[0] == 0x00) break;       /* No more entries */
        if (dir[i].name[0] == 0xE5) continue;    /* Deleted */
        if (dir[i].attr & 0x08) continue;         /* Volume label */
        if (dir[i].attr & 0x10) continue;         /* Subdirectory (skip for now) */

        format_name(dir[i].name, buf[count].name);
        buf[count].size = dir[i].size;
        buf[count].attr = dir[i].attr;
        count++;
    }

    return count;
}

int fat12_read_file(const char *name, uint8_t *buf, uint32_t buf_size) {
    uint8_t name83[11];
    name_to_83(name, name83);

    struct fat12_raw_dirent *dir = (struct fat12_raw_dirent *)(vol + root_dir_offset);

    for (uint16_t i = 0; i < root_entry_count; i++) {
        if (dir[i].name[0] == 0x00) break;
        if (dir[i].name[0] == 0xE5) continue;
        if (dir[i].attr & 0x08) continue;
        if (dir[i].attr & 0x10) continue;

        /* Compare 8.3 names */
        bool match = true;
        for (int j = 0; j < 11; j++) {
            if (dir[i].name[j] != name83[j]) { match = false; break; }
        }
        if (!match) continue;

        /* Found it — read cluster chain */
        uint32_t file_size = dir[i].size;
        uint32_t to_read = file_size < buf_size ? file_size : buf_size;
        uint32_t read = 0;
        uint16_t cluster = dir[i].first_cluster;
        uint32_t cluster_bytes = sectors_per_cluster * bytes_per_sector;

        uint32_t max_clusters = (total_sectors * bytes_per_sector - data_start_offset) / cluster_bytes;
        uint32_t iterations = 0;
        while (read < to_read && cluster >= 2 && cluster < 0xFF8 && iterations++ < max_clusters) {
            uint32_t offset = cluster_to_offset(cluster);
            uint32_t chunk = cluster_bytes;
            if (read + chunk > to_read) chunk = to_read - read;
            memcpy(buf + read, vol + offset, chunk);
            read += chunk;
            cluster = fat12_read_fat(cluster);
        }

        return (int)read;
    }

    return -1;  /* Not found */
}
