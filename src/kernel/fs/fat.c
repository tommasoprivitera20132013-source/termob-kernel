#include "../include/fat.h"

#include "../include/block.h"
#include "../include/klog.h"
#include "../include/serial.h"

#define FAT_ATTR_READ_ONLY 0x01U
#define FAT_ATTR_HIDDEN 0x02U
#define FAT_ATTR_SYSTEM 0x04U
#define FAT_ATTR_VOLUME_ID 0x08U
#define FAT_ATTR_DIRECTORY 0x10U
#define FAT_ATTR_ARCHIVE 0x20U
#define FAT_ATTR_LFN 0x0FU

#define FAT_DIR_ENTRY_SIZE 32U
#define FAT_NAME_LENGTH 11U
#define FAT_MAX_VISITED_CLUSTERS 256U

typedef struct fat_bpb_common {
    uint8_t jump[3];
    uint8_t oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors16;
    uint8_t media;
    uint16_t sectors_per_fat16;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
} __attribute__((packed)) fat_bpb_common_t;

typedef struct fat_bpb_32 {
    fat_bpb_common_t common;
    uint32_t sectors_per_fat32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved_nt;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t type_label[8];
} __attribute__((packed)) fat_bpb_32_t;

typedef struct fat_dir_entry_raw {
    uint8_t name[11];
    uint8_t attributes;
    uint8_t nt_reserved;
    uint8_t create_time_tenths;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat_dir_entry_raw_t;

typedef struct fat_lfn_entry_raw {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attributes;
    uint8_t entry_type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t first_cluster_low;
    uint16_t name3[2];
} __attribute__((packed)) fat_lfn_entry_raw_t;

typedef struct fat_find_context {
    const char* name;
    termob_fat_dirent_t* out_dirent;
    uint8_t found;
} fat_find_context_t;

typedef struct fat_lfn_state {
    uint8_t active;
    uint8_t total_slots;
    uint8_t checksum;
    uint32_t seen_mask;
    char name[TERMOB_FAT_NAME_MAX + 1U];
} fat_lfn_state_t;

static void fat_log_event(const char* message) {
    klog_writeline(message);
    serial_writeline(message);
}

static uint16_t fat_read_le16(const void* src) {
    const uint8_t* bytes;

    bytes = (const uint8_t*)src;
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t fat_read_le32(const void* src) {
    const uint8_t* bytes;

    bytes = (const uint8_t*)src;
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int fat_is_power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static int fat_block_read_sector(const termob_fat_fs_t* fs, uint32_t lba, uint8_t* out_buffer) {
    return block_read_device(fs->device_index, lba, 1U, out_buffer);
}

static int fat_cluster_is_valid(const termob_fat_fs_t* fs, uint32_t cluster) {
    return cluster >= 2U && cluster < (fs->cluster_count + 2U);
}

static int fat_entry_is_end(termob_fat_type_t type, uint32_t value) {
    switch (type) {
        case TERMOB_FAT_TYPE_12:
            return value >= 0x0FF8U;
        case TERMOB_FAT_TYPE_16:
            return value >= 0xFFF8U;
        case TERMOB_FAT_TYPE_32:
            return value >= 0x0FFFFFF8U;
        default:
            return 0;
    }
}

static int fat_entry_is_bad(termob_fat_type_t type, uint32_t value) {
    switch (type) {
        case TERMOB_FAT_TYPE_12:
            return value == 0x0FF7U;
        case TERMOB_FAT_TYPE_16:
            return value == 0xFFF7U;
        case TERMOB_FAT_TYPE_32:
            return value == 0x0FFFFFF7U;
        default:
            return 0;
    }
}

static int fat_entry_is_reserved(termob_fat_type_t type, uint32_t value) {
    switch (type) {
        case TERMOB_FAT_TYPE_12:
            return value >= 0x0FF0U && value <= 0x0FF6U;
        case TERMOB_FAT_TYPE_16:
            return value >= 0xFFF0U && value <= 0xFFF6U;
        case TERMOB_FAT_TYPE_32:
            return value >= 0x0FFFFFF0U && value <= 0x0FFFFFF6U;
        default:
            return 0;
    }
}

static void fat_lfn_reset(fat_lfn_state_t* state) {
    size_t index;

    if (state == 0) {
        return;
    }

    state->active = 0U;
    state->total_slots = 0U;
    state->checksum = 0U;
    state->seen_mask = 0U;
    for (index = 0U; index < sizeof(state->name); index++) {
        state->name[index] = '\0';
    }
}

static uint8_t fat_short_name_checksum(const uint8_t raw_name[11]) {
    uint8_t checksum;
    size_t index;

    checksum = 0U;
    for (index = 0U; index < FAT_NAME_LENGTH; index++) {
        checksum = (uint8_t)(((checksum & 1U) != 0U ? 0x80U : 0U) +
                             (checksum >> 1U) +
                             raw_name[index]);
    }

    return checksum;
}

static char fat_lfn_ascii_char(uint16_t code_unit) {
    if (code_unit <= 0x007EU) {
        return (char)code_unit;
    }

    return '?';
}

static void fat_lfn_store_name_part(fat_lfn_state_t* state,
                                    uint8_t slot_index,
                                    uint8_t char_index,
                                    uint16_t code_unit) {
    size_t target_index;

    if (state == 0 || slot_index == 0U) {
        return;
    }

    target_index = (size_t)(slot_index - 1U) * 13U + (size_t)char_index;
    if (target_index >= TERMOB_FAT_NAME_MAX) {
        return;
    }

    if (code_unit == 0x0000U) {
        state->name[target_index] = '\0';
        return;
    }

    if (code_unit == 0xFFFFU) {
        return;
    }

    state->name[target_index] = fat_lfn_ascii_char(code_unit);
}

static int fat_lfn_collect(fat_lfn_state_t* state, const fat_lfn_entry_raw_t* raw) {
    uint8_t order;
    uint8_t slot_index;
    uint8_t is_last;
    uint32_t expected_mask;
    uint8_t i;

    if (state == 0 || raw == 0 || raw->attributes != FAT_ATTR_LFN || raw->entry_type != 0U ||
        fat_read_le16(&raw->first_cluster_low) != 0U) {
        fat_lfn_reset(state);
        return 0;
    }

    order = (uint8_t)(raw->order & 0x1FU);
    is_last = (uint8_t)(raw->order & 0x40U);
    if (order == 0U || order > 20U) {
        fat_lfn_reset(state);
        return 0;
    }

    if (is_last != 0U) {
        fat_lfn_reset(state);
        state->active = 1U;
        state->total_slots = order;
        state->checksum = raw->checksum;
    } else if (state->active == 0U || state->total_slots == 0U || order > state->total_slots ||
               raw->checksum != state->checksum) {
        fat_lfn_reset(state);
        return 0;
    }

    slot_index = order;
    if (slot_index >= 1U && slot_index <= 20U) {
        state->seen_mask |= (1U << (slot_index - 1U));
    }

    for (i = 0U; i < 5U; i++) {
        fat_lfn_store_name_part(state, slot_index, i, fat_read_le16(&raw->name1[i]));
    }
    for (i = 0U; i < 6U; i++) {
        fat_lfn_store_name_part(state, slot_index, (uint8_t)(5U + i), fat_read_le16(&raw->name2[i]));
    }
    for (i = 0U; i < 2U; i++) {
        fat_lfn_store_name_part(state, slot_index, (uint8_t)(11U + i), fat_read_le16(&raw->name3[i]));
    }

    expected_mask = state->total_slots >= 32U ? 0xFFFFFFFFU : ((1U << state->total_slots) - 1U);
    return state->active != 0U && (state->seen_mask & expected_mask) == expected_mask;
}

static void fat_copy_83_name(char* out_name, const uint8_t raw_name[11]) {
    size_t base_end;
    size_t ext_end;
    size_t cursor;
    size_t i;
    char c;

    base_end = 8U;
    while (base_end > 0U && raw_name[base_end - 1U] == ' ') {
        base_end--;
    }

    ext_end = 3U;
    while (ext_end > 0U && raw_name[8U + ext_end - 1U] == ' ') {
        ext_end--;
    }

    cursor = 0U;
    for (i = 0U; i < base_end; i++) {
        c = (char)raw_name[i];
        if (i == 0U && raw_name[i] == 0x05U) {
            c = (char)0xE5;
        }
        if ((unsigned char)c < 32U || (unsigned char)c > 126U) {
            c = '_';
        }
        out_name[cursor++] = c;
    }

    if (ext_end > 0U) {
        out_name[cursor++] = '.';
        for (i = 0U; i < ext_end; i++) {
            c = (char)raw_name[8U + i];
            if ((unsigned char)c < 32U || (unsigned char)c > 126U) {
                c = '_';
            }
            out_name[cursor++] = c;
        }
    }

    if (cursor == 0U) {
        out_name[cursor++] = '?';
    }

    out_name[cursor] = '\0';
}

static char fat_ascii_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }

    return c;
}

static int fat_name_equals_case_insensitive(const char* a, const char* b) {
    size_t index;

    index = 0U;
    while (a[index] != '\0' && b[index] != '\0') {
        if (fat_ascii_upper(a[index]) != fat_ascii_upper(b[index])) {
            return 0;
        }
        index++;
    }

    return a[index] == '\0' && b[index] == '\0';
}

static int fat_dirent_name_matches(const termob_fat_dirent_t* dirent, const char* name) {
    if (dirent == 0 || name == 0) {
        return 0;
    }

    if (dirent->has_long_name != 0U &&
        fat_name_equals_case_insensitive(dirent->display_name, name)) {
        return 1;
    }

    return fat_name_equals_case_insensitive(dirent->short_name, name);
}

static const char* fat_skip_path_separators(const char* path) {
    while (*path == '/') {
        path++;
    }

    return path;
}

static int fat_is_valid_path_char(char c) {
    return c >= 32 && c <= 126 && c != '/';
}

static int fat_parse_path_component(const char** path_cursor,
                                    char* out_component,
                                    size_t out_component_size,
                                    uint8_t* out_has_more) {
    const char* cursor;
    size_t length;

    if (path_cursor == 0 || *path_cursor == 0 || out_component == 0 ||
        out_component_size < 2U || out_has_more == 0) {
        return 0;
    }

    cursor = fat_skip_path_separators(*path_cursor);
    if (*cursor == '\0') {
        out_component[0] = '\0';
        *path_cursor = cursor;
        *out_has_more = 0U;
        return 1;
    }

    length = 0U;
    while (*cursor != '\0' && *cursor != '/') {
        if (!fat_is_valid_path_char(*cursor) || length + 1U >= out_component_size) {
            return 0;
        }

        out_component[length++] = *cursor++;
    }

    out_component[length] = '\0';
    cursor = fat_skip_path_separators(cursor);
    *out_has_more = *cursor != '\0' ? 1U : 0U;
    *path_cursor = cursor;
    return 1;
}

static void fat_dirent_from_raw(const fat_dir_entry_raw_t* raw, termob_fat_dirent_t* out_dirent) {
    size_t i;
    uint32_t cluster_high;
    uint32_t cluster_low;

    for (i = 0U; i < FAT_NAME_LENGTH; i++) {
        out_dirent->raw_name[i] = raw->name[i];
    }

    fat_copy_83_name(out_dirent->short_name, raw->name);
    fat_copy_83_name(out_dirent->display_name, raw->name);
    out_dirent->attributes = raw->attributes;
    cluster_high = (uint32_t)fat_read_le16(&raw->first_cluster_high);
    cluster_low = (uint32_t)fat_read_le16(&raw->first_cluster_low);
    out_dirent->first_cluster = (cluster_high << 16) | cluster_low;
    out_dirent->size_bytes = fat_read_le32(&raw->file_size);
    out_dirent->has_long_name = 0U;
    out_dirent->suspicious = 0U;

    if (raw->name[0] == 0xE5U) {
        out_dirent->kind = TERMOB_FAT_DIRENT_DELETED;
        return;
    }

    if (raw->attributes == FAT_ATTR_LFN) {
        out_dirent->kind = TERMOB_FAT_DIRENT_LFN;
        return;
    }

    if ((raw->attributes & FAT_ATTR_VOLUME_ID) != 0U) {
        out_dirent->kind = TERMOB_FAT_DIRENT_VOLUME_LABEL;
        return;
    }

    if ((raw->attributes & FAT_ATTR_DIRECTORY) != 0U) {
        out_dirent->kind = TERMOB_FAT_DIRENT_DIRECTORY;
        return;
    }

    if ((raw->attributes & 0xC0U) != 0U) {
        out_dirent->kind = TERMOB_FAT_DIRENT_RESERVED;
        out_dirent->suspicious = 1U;
        return;
    }

    out_dirent->kind = TERMOB_FAT_DIRENT_FILE;
}

static int fat_visit_sector_entries(const termob_fat_fs_t* fs,
                                    const uint8_t* sector,
                                    fat_lfn_state_t* lfn_state,
                                    termob_fat_dirent_callback_t callback,
                                    void* user,
                                    int* out_reached_end);

static int fat_find_entry_callback(const termob_fat_fs_t* fs,
                                   const termob_fat_dirent_t* dirent,
                                   void* user) {
    fat_find_context_t* context;

    (void)fs;

    context = (fat_find_context_t*)user;
    if (dirent->kind == TERMOB_FAT_DIRENT_DELETED ||
        dirent->kind == TERMOB_FAT_DIRENT_LFN ||
        dirent->kind == TERMOB_FAT_DIRENT_VOLUME_LABEL ||
        dirent->kind == TERMOB_FAT_DIRENT_RESERVED) {
        return 1;
    }

    if (!fat_dirent_name_matches(dirent, context->name)) {
        return 1;
    }

    *context->out_dirent = *dirent;
    context->found = 1U;
    return 0;
}

static termob_fat_status_t fat_find_root_fixed(const termob_fat_fs_t* fs,
                                               const char* name,
                                               termob_fat_dirent_t* out_dirent) {
    fat_find_context_t context;
    fat_lfn_state_t lfn_state;
    uint8_t sector[4096];
    uint32_t lba;

    context.name = name;
    context.out_dirent = out_dirent;
    context.found = 0U;
    fat_lfn_reset(&lfn_state);

    for (lba = fs->first_root_dir_lba; lba < fs->first_root_dir_lba + fs->root_dir_sectors; lba++) {
        int reached_end;

        if (!fat_block_read_sector(fs, lba, sector)) {
            fat_log_event("TERMOB: fat path root read failed");
            return TERMOB_FAT_STATUS_IO_ERROR;
        }

        if (!fat_visit_sector_entries(fs,
                                      sector,
                                      &lfn_state,
                                      fat_find_entry_callback,
                                      &context,
                                      &reached_end)) {
            return TERMOB_FAT_STATUS_CORRUPT;
        }

        if (context.found != 0U) {
            return out_dirent->suspicious ? TERMOB_FAT_STATUS_CORRUPT : TERMOB_FAT_STATUS_OK;
        }

        if (reached_end) {
            return TERMOB_FAT_STATUS_NOT_FOUND;
        }
    }

    return TERMOB_FAT_STATUS_NOT_FOUND;
}

static termob_fat_status_t fat_find_directory_cluster_chain(const termob_fat_fs_t* fs,
                                                            uint32_t start_cluster,
                                                            const char* name,
                                                            termob_fat_dirent_t* out_dirent,
                                                            const char* read_fail_message,
                                                            const char* next_fail_message,
                                                            const char* loop_message,
                                                            const char* long_message,
                                                            const char* corrupt_message,
                                                            const char* invalid_message) {
    fat_find_context_t context;
    fat_lfn_state_t lfn_state;
    uint32_t visited[FAT_MAX_VISITED_CLUSTERS];
    uint8_t sector[4096];
    uint32_t cluster;
    uint32_t visited_count;

    context.name = name;
    context.out_dirent = out_dirent;
    context.found = 0U;
    fat_lfn_reset(&lfn_state);
    cluster = start_cluster;
    visited_count = 0U;

    while (fat_cluster_is_valid(fs, cluster)) {
        uint32_t i;
        uint32_t sector_offset;
        uint32_t next_cluster;

        for (i = 0U; i < visited_count; i++) {
            if (visited[i] == cluster) {
                fat_log_event(loop_message);
                return TERMOB_FAT_STATUS_CORRUPT;
            }
        }

        if (visited_count >= FAT_MAX_VISITED_CLUSTERS) {
            fat_log_event(long_message);
            return TERMOB_FAT_STATUS_CORRUPT;
        }

        visited[visited_count++] = cluster;
        for (sector_offset = 0U; sector_offset < fs->sectors_per_cluster; sector_offset++) {
            int reached_end;

            if (!fat_block_read_sector(fs, fat_cluster_first_lba(fs, cluster) + sector_offset, sector)) {
                fat_log_event(read_fail_message);
                return TERMOB_FAT_STATUS_IO_ERROR;
            }

            if (!fat_visit_sector_entries(fs,
                                          sector,
                                          &lfn_state,
                                          fat_find_entry_callback,
                                          &context,
                                          &reached_end)) {
                return TERMOB_FAT_STATUS_CORRUPT;
            }

            if (context.found != 0U) {
                return out_dirent->suspicious ? TERMOB_FAT_STATUS_CORRUPT : TERMOB_FAT_STATUS_OK;
            }

            if (reached_end) {
                return TERMOB_FAT_STATUS_NOT_FOUND;
            }
        }

        if (!fat_read_fat_entry(fs, cluster, &next_cluster)) {
            fat_log_event(next_fail_message);
            return TERMOB_FAT_STATUS_IO_ERROR;
        }

        if (fat_entry_is_end(fs->fat_type, next_cluster)) {
            return TERMOB_FAT_STATUS_NOT_FOUND;
        }

        if (next_cluster == 0U || fat_entry_is_bad(fs->fat_type, next_cluster) ||
            fat_entry_is_reserved(fs->fat_type, next_cluster) ||
            !fat_cluster_is_valid(fs, next_cluster)) {
            fat_log_event(corrupt_message);
            return TERMOB_FAT_STATUS_CORRUPT;
        }

        cluster = next_cluster;
    }

    fat_log_event(invalid_message);
    return TERMOB_FAT_STATUS_CORRUPT;
}

static termob_fat_status_t fat_find_root_entry(const termob_fat_fs_t* fs,
                                               const char* name,
                                               termob_fat_dirent_t* out_dirent) {
    if (fs->fat_type == TERMOB_FAT_TYPE_12 || fs->fat_type == TERMOB_FAT_TYPE_16) {
        return fat_find_root_fixed(fs, name, out_dirent);
    }

    if (fs->fat_type == TERMOB_FAT_TYPE_32) {
        return fat_find_directory_cluster_chain(fs,
                                                fs->root_cluster,
                                                name,
                                                out_dirent,
                                                "TERMOB: fat path root read failed",
                                                "TERMOB: fat path root next cluster read failed",
                                                "TERMOB: fat path root loop detected",
                                                "TERMOB: fat path root chain too long for guard table",
                                                "TERMOB: fat path root chain corrupted",
                                                "TERMOB: fat path root cluster invalid");
    }

    return TERMOB_FAT_STATUS_CORRUPT;
}

static termob_fat_status_t fat_find_directory_entry(const termob_fat_fs_t* fs,
                                                    uint32_t start_cluster,
                                                    const char* name,
                                                    termob_fat_dirent_t* out_dirent) {
    if (!fat_cluster_is_valid(fs, start_cluster)) {
        fat_log_event("TERMOB: fat path directory cluster invalid");
        return TERMOB_FAT_STATUS_CORRUPT;
    }

    return fat_find_directory_cluster_chain(fs,
                                            start_cluster,
                                            name,
                                            out_dirent,
                                            "TERMOB: fat path directory read failed",
                                            "TERMOB: fat path directory next cluster read failed",
                                            "TERMOB: fat path directory loop detected",
                                            "TERMOB: fat path directory chain too long for guard table",
                                            "TERMOB: fat path directory chain corrupted",
                                            "TERMOB: fat path directory cluster invalid");
}

static int fat_visit_sector_entries(const termob_fat_fs_t* fs,
                                    const uint8_t* sector,
                                    fat_lfn_state_t* lfn_state,
                                    termob_fat_dirent_callback_t callback,
                                    void* user,
                                    int* out_reached_end) {
    size_t offset;

    if (out_reached_end != 0) {
        *out_reached_end = 0;
    }

    for (offset = 0U; offset < fs->bytes_per_sector; offset += FAT_DIR_ENTRY_SIZE) {
        const fat_dir_entry_raw_t* raw;
        termob_fat_dirent_t dirent;
        uint8_t short_name_checksum;

        raw = (const fat_dir_entry_raw_t*)(sector + offset);
        if (raw->name[0] == 0x00U) {
            fat_lfn_reset(lfn_state);
            if (out_reached_end != 0) {
                *out_reached_end = 1;
            }
            return 1;
        }

        if (raw->name[0] == 0xE5U) {
            fat_lfn_reset(lfn_state);
            fat_dirent_from_raw(raw, &dirent);
            if (callback != 0 && !callback(fs, &dirent, user)) {
                return 1;
            }
            continue;
        }

        if (raw->attributes == FAT_ATTR_LFN) {
            fat_lfn_collect(lfn_state, (const fat_lfn_entry_raw_t*)(const void*)raw);
            continue;
        }

        fat_dirent_from_raw(raw, &dirent);
        short_name_checksum = fat_short_name_checksum(raw->name);
        if (lfn_state != 0 &&
            lfn_state->active != 0U &&
            lfn_state->checksum == short_name_checksum &&
            lfn_state->name[0] != '\0') {
            size_t index;

            for (index = 0U; index < TERMOB_FAT_NAME_MAX; index++) {
                dirent.display_name[index] = lfn_state->name[index];
                if (lfn_state->name[index] == '\0') {
                    break;
                }
            }
            dirent.display_name[TERMOB_FAT_NAME_MAX] = '\0';
            dirent.has_long_name = 1U;
        }
        fat_lfn_reset(lfn_state);

        if ((dirent.kind == TERMOB_FAT_DIRENT_FILE ||
             dirent.kind == TERMOB_FAT_DIRENT_DIRECTORY) &&
            dirent.first_cluster != 0U &&
            !fat_cluster_is_valid(fs, dirent.first_cluster)) {
            dirent.suspicious = 1U;
        }

        if (callback != 0 && !callback(fs, &dirent, user)) {
            return 1;
        }
    }

    return 1;
}

static int fat_list_root_fixed(const termob_fat_fs_t* fs,
                               termob_fat_dirent_callback_t callback,
                               void* user) {
    fat_lfn_state_t lfn_state;
    uint8_t sector[4096];
    uint32_t lba;

    fat_lfn_reset(&lfn_state);

    for (lba = fs->first_root_dir_lba; lba < fs->first_root_dir_lba + fs->root_dir_sectors; lba++) {
        int reached_end;

        if (!fat_block_read_sector(fs, lba, sector)) {
            fat_log_event("TERMOB: fat root read failed");
            return 0;
        }

        if (!fat_visit_sector_entries(fs, sector, &lfn_state, callback, user, &reached_end)) {
            return 0;
        }

        if (reached_end) {
            return 1;
        }
    }

    return 1;
}

static int fat_list_cluster_chain(const termob_fat_fs_t* fs,
                                  uint32_t start_cluster,
                                  termob_fat_dirent_callback_t callback,
                                  void* user,
                                  const char* read_fail_message,
                                  const char* next_fail_message,
                                  const char* loop_message,
                                  const char* long_message,
                                  const char* corrupt_message,
                                  const char* invalid_message) {
    fat_lfn_state_t lfn_state;
    uint32_t visited[FAT_MAX_VISITED_CLUSTERS];
    uint8_t sector[4096];
    uint32_t cluster;
    uint32_t visited_count;

    cluster = start_cluster;
    visited_count = 0U;
    fat_lfn_reset(&lfn_state);

    while (fat_cluster_is_valid(fs, cluster)) {
        uint32_t sector_offset;
        uint32_t next_cluster;
        uint32_t i;

        for (i = 0U; i < visited_count; i++) {
            if (visited[i] == cluster) {
                fat_log_event(loop_message);
                return 0;
            }
        }

        if (visited_count >= FAT_MAX_VISITED_CLUSTERS) {
            fat_log_event(long_message);
            return 0;
        }

        visited[visited_count++] = cluster;
        for (sector_offset = 0U; sector_offset < fs->sectors_per_cluster; sector_offset++) {
            int reached_end;

            if (!fat_block_read_sector(fs, fat_cluster_first_lba(fs, cluster) + sector_offset, sector)) {
                fat_log_event(read_fail_message);
                return 0;
            }

            if (!fat_visit_sector_entries(fs, sector, &lfn_state, callback, user, &reached_end)) {
                return 0;
            }

            if (reached_end) {
                return 1;
            }
        }

        if (!fat_read_fat_entry(fs, cluster, &next_cluster)) {
            fat_log_event(next_fail_message);
            return 0;
        }

        if (fat_entry_is_end(fs->fat_type, next_cluster)) {
            return 1;
        }
        if (next_cluster == 0U || fat_entry_is_bad(fs->fat_type, next_cluster) ||
            fat_entry_is_reserved(fs->fat_type, next_cluster) ||
            !fat_cluster_is_valid(fs, next_cluster)) {
            fat_log_event(corrupt_message);
            return 0;
        }

        cluster = next_cluster;
    }

    fat_log_event(invalid_message);
    return 0;
}

const char* fat_type_name(termob_fat_type_t type) {
    switch (type) {
        case TERMOB_FAT_TYPE_12:
            return "FAT12";
        case TERMOB_FAT_TYPE_16:
            return "FAT16";
        case TERMOB_FAT_TYPE_32:
            return "FAT32";
        default:
            return "unknown";
    }
}

const char* fat_dirent_kind_name(termob_fat_dirent_kind_t kind) {
    switch (kind) {
        case TERMOB_FAT_DIRENT_FILE:
            return "file";
        case TERMOB_FAT_DIRENT_DIRECTORY:
            return "directory";
        case TERMOB_FAT_DIRENT_VOLUME_LABEL:
            return "volume";
        case TERMOB_FAT_DIRENT_LFN:
            return "lfn";
        case TERMOB_FAT_DIRENT_DELETED:
            return "deleted";
        case TERMOB_FAT_DIRENT_RESERVED:
            return "reserved";
        default:
            return "unknown";
    }
}

const char* fat_status_name(termob_fat_status_t status) {
    switch (status) {
        case TERMOB_FAT_STATUS_OK:
            return "ok";
        case TERMOB_FAT_STATUS_INVALID_ARGUMENT:
            return "invalid argument";
        case TERMOB_FAT_STATUS_INVALID_PATH:
            return "invalid path";
        case TERMOB_FAT_STATUS_NOT_FOUND:
            return "not found";
        case TERMOB_FAT_STATUS_NOT_DIRECTORY:
            return "not a directory";
        case TERMOB_FAT_STATUS_NOT_FILE:
            return "not a file";
        case TERMOB_FAT_STATUS_CORRUPT:
            return "corrupt";
        case TERMOB_FAT_STATUS_IO_ERROR:
            return "io error";
        default:
            return "unknown";
    }
}

int fat_mount(termob_fat_fs_t* fs, size_t device_index, uint32_t boot_lba) {
    termob_block_device_t device;
    uint8_t sector[4096];
    const fat_bpb_common_t* bpb;
    uint32_t metadata_sectors;
    uint32_t root_dir_sectors;
    uint32_t data_sectors;
    uint32_t cluster_count;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;

    if (fs == 0) {
        return 0;
    }

    if (!block_device_at(device_index, &device)) {
        fat_log_event("TERMOB: fat mount failed (device missing)");
        return 0;
    }

    if (!fat_block_read_sector(&(termob_fat_fs_t){ .device_index = device_index }, boot_lba, sector)) {
        fat_log_event("TERMOB: fat boot sector read failed");
        return 0;
    }

    bpb = (const fat_bpb_common_t*)sector;
    fs->device_index = device_index;
    fs->boot_lba = boot_lba;
    fs->bytes_per_sector = (uint32_t)fat_read_le16(&bpb->bytes_per_sector);
    fs->sectors_per_cluster = (uint32_t)bpb->sectors_per_cluster;
    fs->reserved_sectors = (uint32_t)fat_read_le16(&bpb->reserved_sectors);
    fs->fat_count = (uint32_t)bpb->fat_count;
    fs->root_entry_count = (uint32_t)fat_read_le16(&bpb->root_entry_count);
    fs->total_sectors = (uint32_t)fat_read_le16(&bpb->total_sectors16);
    if (fs->total_sectors == 0U) {
        fs->total_sectors = fat_read_le32(&bpb->total_sectors32);
    }

    sectors_per_fat = (uint32_t)fat_read_le16(&bpb->sectors_per_fat16);
    if (sectors_per_fat == 0U) {
        const fat_bpb_32_t* bpb32;

        bpb32 = (const fat_bpb_32_t*)sector;
        sectors_per_fat = fat_read_le32(&bpb32->sectors_per_fat32);
        fs->root_cluster = fat_read_le32(&bpb32->root_cluster);
    } else {
        fs->root_cluster = 0U;
    }
    fs->sectors_per_fat = sectors_per_fat;

    if (!(fs->bytes_per_sector == 512U ||
          fs->bytes_per_sector == 1024U ||
          fs->bytes_per_sector == 2048U ||
          fs->bytes_per_sector == 4096U)) {
        fat_log_event("TERMOB: fat mount failed (invalid bytes/sector)");
        return 0;
    }

    if (fs->bytes_per_sector != device.sector_size_bytes) {
        fat_log_event("TERMOB: fat mount failed (sector size mismatch)");
        return 0;
    }

    if (!fat_is_power_of_two(fs->sectors_per_cluster) || fs->sectors_per_cluster > 128U) {
        fat_log_event("TERMOB: fat mount failed (invalid sectors/cluster)");
        return 0;
    }

    if (fs->reserved_sectors == 0U || fs->fat_count == 0U || fs->sectors_per_fat == 0U ||
        fs->total_sectors == 0U || sector[510] != 0x55U || sector[511] != 0xAAU) {
        fat_log_event("TERMOB: fat mount failed (invalid BPB)");
        return 0;
    }

    root_dir_sectors =
        ((fs->root_entry_count * 32U) + (fs->bytes_per_sector - 1U)) / fs->bytes_per_sector;
    total_sectors = fs->total_sectors;
    metadata_sectors = fs->reserved_sectors +
                       (fs->fat_count * fs->sectors_per_fat) +
                       root_dir_sectors;
    if (total_sectors <= metadata_sectors) {
        fat_log_event("TERMOB: fat mount failed (layout outside volume)");
        return 0;
    }

    data_sectors = total_sectors - metadata_sectors;
    cluster_count = data_sectors / fs->sectors_per_cluster;
    if (cluster_count == 0U) {
        fat_log_event("TERMOB: fat mount failed (no data clusters)");
        return 0;
    }

    if (cluster_count < 4085U) {
        fs->fat_type = TERMOB_FAT_TYPE_12;
    } else if (cluster_count < 65525U) {
        fs->fat_type = TERMOB_FAT_TYPE_16;
    } else {
        fs->fat_type = TERMOB_FAT_TYPE_32;
    }

    fs->cluster_count = cluster_count;
    fs->root_dir_sectors = root_dir_sectors;
    fs->first_fat_lba = fs->boot_lba + fs->reserved_sectors;
    fs->first_root_dir_lba = fs->first_fat_lba + (fs->fat_count * fs->sectors_per_fat);
    fs->first_data_lba = fs->first_root_dir_lba + root_dir_sectors;

    if (fs->fat_type == TERMOB_FAT_TYPE_32) {
        fs->first_root_dir_lba = 0U;
        if (!fat_cluster_is_valid(fs, fs->root_cluster)) {
            fat_log_event("TERMOB: fat mount failed (invalid FAT32 root cluster)");
            return 0;
        }
    }

    return 1;
}

int fat_read_fat_entry(const termob_fat_fs_t* fs, uint32_t cluster, uint32_t* out_value) {
    uint8_t entry_buffer[8192];
    uint32_t fat_offset;
    uint32_t sector_index;
    uint32_t byte_offset;
    uint32_t sectors_to_read;
    uint32_t value;

    if (fs == 0 || out_value == 0 || fs->bytes_per_sector > sizeof(entry_buffer)) {
        return 0;
    }

    switch (fs->fat_type) {
        case TERMOB_FAT_TYPE_12:
            fat_offset = cluster + (cluster / 2U);
            break;
        case TERMOB_FAT_TYPE_16:
            fat_offset = cluster * 2U;
            break;
        case TERMOB_FAT_TYPE_32:
            fat_offset = cluster * 4U;
            break;
        default:
            return 0;
    }

    sector_index = fat_offset / fs->bytes_per_sector;
    byte_offset = fat_offset % fs->bytes_per_sector;
    sectors_to_read = 1U;

    if ((fs->fat_type == TERMOB_FAT_TYPE_12 && byte_offset == fs->bytes_per_sector - 1U) ||
        (fs->fat_type == TERMOB_FAT_TYPE_16 && byte_offset > fs->bytes_per_sector - 2U) ||
        (fs->fat_type == TERMOB_FAT_TYPE_32 && byte_offset > fs->bytes_per_sector - 4U)) {
        sectors_to_read = 2U;
    }

    if (!block_read_device(fs->device_index,
                           fs->first_fat_lba + sector_index,
                           sectors_to_read,
                           entry_buffer)) {
        return 0;
    }

    switch (fs->fat_type) {
        case TERMOB_FAT_TYPE_12: {
            uint16_t raw;

            raw = fat_read_le16(entry_buffer + byte_offset);
            value = (cluster & 1U) == 0U ? (uint32_t)(raw & 0x0FFFU)
                                         : (uint32_t)((raw >> 4) & 0x0FFFU);
            break;
        }
        case TERMOB_FAT_TYPE_16:
            value = (uint32_t)fat_read_le16(entry_buffer + byte_offset);
            break;
        case TERMOB_FAT_TYPE_32:
            value = fat_read_le32(entry_buffer + byte_offset) & 0x0FFFFFFFU;
            break;
        default:
            return 0;
    }

    *out_value = value;
    return 1;
}

uint32_t fat_cluster_first_lba(const termob_fat_fs_t* fs, uint32_t cluster) {
    return fs->first_data_lba + ((cluster - 2U) * fs->sectors_per_cluster);
}

static size_t fat_min_size(size_t a, size_t b) {
    return a < b ? a : b;
}

static void fat_copy_bytes(uint8_t* dst, const uint8_t* src, size_t count) {
    size_t index;

    for (index = 0U; index < count; index++) {
        dst[index] = src[index];
    }
}

static uint32_t fat_file_cluster_size_bytes(const termob_fat_file_t* file) {
    return file->fs.bytes_per_sector * file->fs.sectors_per_cluster;
}

static void fat_file_reset_state(termob_fat_file_t* file) {
    if (file == 0) {
        return;
    }

    file->position = 0U;
    file->current_cluster = file->dirent.size_bytes == 0U ? 0U : file->dirent.first_cluster;
    file->current_cluster_file_offset = 0U;
    file->clusters_seen = file->dirent.size_bytes == 0U ? 0U : 1U;
    file->visited_count = file->dirent.size_bytes == 0U ? 0U : 1U;
    if (file->dirent.size_bytes != 0U) {
        file->visited_clusters[0] = file->dirent.first_cluster;
    }
}

static uint32_t fat_file_max_clusters(const termob_fat_file_t* file) {
    uint32_t cluster_size;

    if (file->dirent.size_bytes == 0U) {
        return 0U;
    }

    cluster_size = fat_file_cluster_size_bytes(file);
    return (file->dirent.size_bytes + cluster_size - 1U) / cluster_size;
}

static termob_fat_status_t fat_file_advance_cluster(termob_fat_file_t* file) {
    uint32_t cluster_size;
    uint32_t max_clusters;
    uint32_t index;
    uint32_t next_cluster;

    if (file == 0 || file->current_cluster == 0U || !fat_cluster_is_valid(&file->fs, file->current_cluster)) {
        fat_log_event("TERMOB: fat file cluster invalid");
        return TERMOB_FAT_STATUS_CORRUPT;
    }

    max_clusters = fat_file_max_clusters(file);
    if (file->clusters_seen >= max_clusters) {
        fat_log_event("TERMOB: fat file chain too long");
        return TERMOB_FAT_STATUS_CORRUPT;
    }

    if (!fat_read_fat_entry(&file->fs, file->current_cluster, &next_cluster)) {
        fat_log_event("TERMOB: fat file next cluster read failed");
        return TERMOB_FAT_STATUS_IO_ERROR;
    }

    if (fat_entry_is_end(file->fs.fat_type, next_cluster)) {
        fat_log_event("TERMOB: fat file chain ended before eof");
        return TERMOB_FAT_STATUS_CORRUPT;
    }

    if (next_cluster == 0U || fat_entry_is_bad(file->fs.fat_type, next_cluster) ||
        fat_entry_is_reserved(file->fs.fat_type, next_cluster) ||
        !fat_cluster_is_valid(&file->fs, next_cluster)) {
        fat_log_event("TERMOB: fat file chain corrupted");
        return TERMOB_FAT_STATUS_CORRUPT;
    }

    for (index = 0U; index < file->visited_count; index++) {
        if (file->visited_clusters[index] == next_cluster) {
            fat_log_event("TERMOB: fat file chain loop detected");
            return TERMOB_FAT_STATUS_CORRUPT;
        }
    }

    if (file->visited_count >= TERMOB_FAT_FILE_MAX_VISITED_CLUSTERS) {
        fat_log_event("TERMOB: fat file chain exceeds loop guard");
        return TERMOB_FAT_STATUS_CORRUPT;
    }

    cluster_size = fat_file_cluster_size_bytes(file);
    file->current_cluster = next_cluster;
    file->current_cluster_file_offset += cluster_size;
    file->clusters_seen++;
    file->visited_clusters[file->visited_count++] = next_cluster;
    return TERMOB_FAT_STATUS_OK;
}

termob_fat_status_t fat_seek_file(termob_fat_file_t* file, uint32_t offset) {
    uint32_t cluster_size;
    uint32_t clusters_to_advance;
    uint32_t i;
    termob_fat_status_t status;

    if (file == 0) {
        return TERMOB_FAT_STATUS_INVALID_ARGUMENT;
    }

    if (offset > file->dirent.size_bytes) {
        return TERMOB_FAT_STATUS_INVALID_ARGUMENT;
    }

    fat_file_reset_state(file);
    if (offset == 0U || file->dirent.size_bytes == 0U) {
        return TERMOB_FAT_STATUS_OK;
    }

    cluster_size = fat_file_cluster_size_bytes(file);
    clusters_to_advance = offset / cluster_size;
    for (i = 0U; i < clusters_to_advance; i++) {
        status = fat_file_advance_cluster(file);
        if (status != TERMOB_FAT_STATUS_OK) {
            return status;
        }
    }

    file->position = offset;
    file->current_cluster_file_offset = clusters_to_advance * cluster_size;
    return TERMOB_FAT_STATUS_OK;
}

termob_fat_status_t fat_lookup_path(const termob_fat_fs_t* fs,
                                    const char* path,
                                    termob_fat_lookup_result_t* out_result) {
    const char* cursor;
    uint8_t has_more;

    if (fs == 0 || path == 0 || out_result == 0) {
        return TERMOB_FAT_STATUS_INVALID_ARGUMENT;
    }

    if (path[0] != '/') {
        fat_log_event("TERMOB: fat path invalid syntax");
        return TERMOB_FAT_STATUS_INVALID_PATH;
    }

    out_result->is_root = 1U;
    cursor = path;
    cursor = fat_skip_path_separators(cursor);
    if (*cursor == '\0') {
        return TERMOB_FAT_STATUS_OK;
    }

    while (*cursor != '\0') {
        char component[13];
        termob_fat_dirent_t entry;
        termob_fat_status_t status;

        if (!fat_parse_path_component(&cursor, component, sizeof(component), &has_more) ||
            component[0] == '\0') {
            fat_log_event("TERMOB: fat path component invalid");
            return TERMOB_FAT_STATUS_INVALID_PATH;
        }

        if (out_result->is_root != 0U) {
            status = fat_find_root_entry(fs, component, &entry);
        } else {
            if (out_result->dirent.kind != TERMOB_FAT_DIRENT_DIRECTORY) {
                fat_log_event("TERMOB: fat path used file as directory");
                return TERMOB_FAT_STATUS_NOT_DIRECTORY;
            }
            status = fat_find_directory_entry(fs,
                                              out_result->dirent.first_cluster,
                                              component,
                                              &entry);
        }

        if (status != TERMOB_FAT_STATUS_OK) {
            return status;
        }

        if (entry.suspicious != 0U) {
            fat_log_event("TERMOB: fat path matched suspicious entry");
            return TERMOB_FAT_STATUS_CORRUPT;
        }

        out_result->dirent = entry;
        out_result->is_root = 0U;
        if (has_more != 0U && entry.kind != TERMOB_FAT_DIRENT_DIRECTORY) {
            fat_log_event("TERMOB: fat path used file as directory");
            return TERMOB_FAT_STATUS_NOT_DIRECTORY;
        }
    }

    return TERMOB_FAT_STATUS_OK;
}

termob_fat_status_t fat_open_file(const termob_fat_fs_t* fs,
                                  const char* path,
                                  termob_fat_file_t* out_file) {
    termob_fat_lookup_result_t lookup;
    termob_fat_status_t status;

    if (fs == 0 || path == 0 || out_file == 0) {
        return TERMOB_FAT_STATUS_INVALID_ARGUMENT;
    }

    status = fat_lookup_path(fs, path, &lookup);
    if (status != TERMOB_FAT_STATUS_OK) {
        return status;
    }

    if (lookup.is_root != 0U || lookup.dirent.kind != TERMOB_FAT_DIRENT_FILE) {
        fat_log_event("TERMOB: fat path target not file");
        return TERMOB_FAT_STATUS_NOT_FILE;
    }

    if (lookup.dirent.suspicious != 0U) {
        fat_log_event("TERMOB: fat file entry suspicious");
        return TERMOB_FAT_STATUS_CORRUPT;
    }

    *out_file = (termob_fat_file_t){
        .fs = *fs,
        .dirent = lookup.dirent,
        .position = 0U,
        .current_cluster = lookup.dirent.size_bytes == 0U ? 0U : lookup.dirent.first_cluster,
        .current_cluster_file_offset = 0U,
        .clusters_seen = lookup.dirent.size_bytes == 0U ? 0U : 1U,
        .visited_count = lookup.dirent.size_bytes == 0U ? 0U : 1U
    };

    if (lookup.dirent.size_bytes != 0U && !fat_cluster_is_valid(fs, lookup.dirent.first_cluster)) {
        fat_log_event("TERMOB: fat file first cluster invalid");
        return TERMOB_FAT_STATUS_CORRUPT;
    }

    if (lookup.dirent.size_bytes != 0U) {
        out_file->visited_clusters[0] = lookup.dirent.first_cluster;
    }

    return TERMOB_FAT_STATUS_OK;
}

termob_fat_status_t fat_read_file(termob_fat_file_t* file,
                                  void* buffer,
                                  size_t buffer_size,
                                  size_t* out_read_bytes) {
    uint8_t sector[4096];
    uint8_t* out_bytes;
    size_t total_to_copy;
    size_t copied;

    if (out_read_bytes != 0) {
        *out_read_bytes = 0U;
    }

    if (file == 0 || (buffer == 0 && buffer_size != 0U)) {
        return TERMOB_FAT_STATUS_INVALID_ARGUMENT;
    }

    if (buffer_size == 0U || file->position >= file->dirent.size_bytes) {
        return TERMOB_FAT_STATUS_OK;
    }

    if (file->fs.bytes_per_sector > sizeof(sector)) {
        fat_log_event("TERMOB: fat file sector buffer too small");
        return TERMOB_FAT_STATUS_CORRUPT;
    }

    out_bytes = (uint8_t*)buffer;
    total_to_copy = fat_min_size(buffer_size, (size_t)(file->dirent.size_bytes - file->position));
    copied = 0U;

    while (copied < total_to_copy) {
        uint32_t cluster_size;
        uint32_t cluster_offset;
        uint32_t sector_index;
        uint32_t sector_byte_offset;
        uint32_t sector_lba;
        size_t bytes_this_round;
        size_t available_in_sector;

        if (file->current_cluster == 0U || !fat_cluster_is_valid(&file->fs, file->current_cluster)) {
            fat_log_event("TERMOB: fat file cluster invalid");
            if (out_read_bytes != 0) {
                *out_read_bytes = copied;
            }
            return TERMOB_FAT_STATUS_CORRUPT;
        }

        cluster_size = fat_file_cluster_size_bytes(file);
        cluster_offset = file->position - file->current_cluster_file_offset;
        if (cluster_offset >= cluster_size) {
            fat_log_event("TERMOB: fat file cursor outside cluster");
            if (out_read_bytes != 0) {
                *out_read_bytes = copied;
            }
            return TERMOB_FAT_STATUS_CORRUPT;
        }

        sector_index = cluster_offset / file->fs.bytes_per_sector;
        sector_byte_offset = cluster_offset % file->fs.bytes_per_sector;
        sector_lba = fat_cluster_first_lba(&file->fs, file->current_cluster) + sector_index;

        if (!fat_block_read_sector(&file->fs, sector_lba, sector)) {
            fat_log_event("TERMOB: fat file data read failed");
            if (out_read_bytes != 0) {
                *out_read_bytes = copied;
            }
            return TERMOB_FAT_STATUS_IO_ERROR;
        }

        available_in_sector = (size_t)(file->fs.bytes_per_sector - sector_byte_offset);
        bytes_this_round = fat_min_size(available_in_sector, total_to_copy - copied);
        fat_copy_bytes(out_bytes + copied, sector + sector_byte_offset, bytes_this_round);
        copied += bytes_this_round;
        file->position += (uint32_t)bytes_this_round;

        if (file->position >= file->dirent.size_bytes) {
            break;
        }

        if ((file->position - file->current_cluster_file_offset) >= cluster_size) {
            termob_fat_status_t status;

            status = fat_file_advance_cluster(file);
            if (status != TERMOB_FAT_STATUS_OK) {
                if (out_read_bytes != 0) {
                    *out_read_bytes = copied;
                }
                return status;
            }
        }
    }

    if (out_read_bytes != 0) {
        *out_read_bytes = copied;
    }
    return TERMOB_FAT_STATUS_OK;
}

termob_fat_status_t fat_read_file_range(const termob_fat_fs_t* fs,
                                        const char* path,
                                        uint32_t offset,
                                        void* buffer,
                                        size_t buffer_size,
                                        size_t* out_read_bytes) {
    termob_fat_file_t file;
    termob_fat_status_t status;

    if (out_read_bytes != 0) {
        *out_read_bytes = 0U;
    }

    if (fs == 0 || path == 0 || (buffer == 0 && buffer_size != 0U)) {
        return TERMOB_FAT_STATUS_INVALID_ARGUMENT;
    }

    status = fat_open_file(fs, path, &file);
    if (status != TERMOB_FAT_STATUS_OK) {
        return status;
    }

    if (offset >= file.dirent.size_bytes) {
        return TERMOB_FAT_STATUS_OK;
    }

    status = fat_seek_file(&file, offset);
    if (status != TERMOB_FAT_STATUS_OK) {
        return status;
    }

    return fat_read_file(&file, buffer, buffer_size, out_read_bytes);
}

termob_fat_status_t fat_list_path(const termob_fat_fs_t* fs,
                                  const char* path,
                                  termob_fat_dirent_callback_t callback,
                                  void* user) {
    termob_fat_lookup_result_t lookup;
    termob_fat_status_t status;

    if (fs == 0 || path == 0) {
        return TERMOB_FAT_STATUS_INVALID_ARGUMENT;
    }

    if (path[0] == '\0') {
        fat_log_event("TERMOB: fat path invalid syntax");
        return TERMOB_FAT_STATUS_INVALID_PATH;
    }

    if (path[0] == '/' && path[1] == '\0') {
        if (!fat_list_root(fs, callback, user)) {
            return TERMOB_FAT_STATUS_CORRUPT;
        }
        return TERMOB_FAT_STATUS_OK;
    }

    status = fat_lookup_path(fs, path, &lookup);
    if (status != TERMOB_FAT_STATUS_OK) {
        return status;
    }

    if (lookup.is_root != 0U) {
        if (!fat_list_root(fs, callback, user)) {
            return TERMOB_FAT_STATUS_CORRUPT;
        }
        return TERMOB_FAT_STATUS_OK;
    }

    if (lookup.dirent.kind != TERMOB_FAT_DIRENT_DIRECTORY) {
        fat_log_event("TERMOB: fat list path target not directory");
        return TERMOB_FAT_STATUS_NOT_DIRECTORY;
    }

    if (!fat_list_directory(fs, lookup.dirent.first_cluster, callback, user)) {
        return TERMOB_FAT_STATUS_CORRUPT;
    }

    return TERMOB_FAT_STATUS_OK;
}

int fat_list_directory(const termob_fat_fs_t* fs,
                       uint32_t start_cluster,
                       termob_fat_dirent_callback_t callback,
                       void* user) {
    if (fs == 0 || !fat_cluster_is_valid(fs, start_cluster)) {
        fat_log_event("TERMOB: fat directory cluster invalid");
        return 0;
    }

    return fat_list_cluster_chain(fs,
                                  start_cluster,
                                  callback,
                                  user,
                                  "TERMOB: fat directory cluster read failed",
                                  "TERMOB: fat directory next cluster read failed",
                                  "TERMOB: fat directory loop detected",
                                  "TERMOB: fat directory chain too long for guard table",
                                  "TERMOB: fat directory chain corrupted",
                                  "TERMOB: fat directory cluster invalid");
}

int fat_list_root(const termob_fat_fs_t* fs,
                  termob_fat_dirent_callback_t callback,
                  void* user) {
    if (fs == 0) {
        return 0;
    }

    if (fs->fat_type == TERMOB_FAT_TYPE_12 || fs->fat_type == TERMOB_FAT_TYPE_16) {
        return fat_list_root_fixed(fs, callback, user);
    }

    if (fs->fat_type == TERMOB_FAT_TYPE_32) {
        return fat_list_cluster_chain(fs,
                                      fs->root_cluster,
                                      callback,
                                      user,
                                      "TERMOB: fat root cluster read failed",
                                      "TERMOB: fat root next cluster read failed",
                                      "TERMOB: fat root chain loop detected",
                                      "TERMOB: fat root chain too long for guard table",
                                      "TERMOB: fat root chain corrupted",
                                      "TERMOB: fat root cluster invalid");
    }

    return 0;
}
