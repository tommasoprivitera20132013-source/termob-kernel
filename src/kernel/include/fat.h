#ifndef TERMOB_FAT_H
#define TERMOB_FAT_H

#include <stddef.h>
#include <stdint.h>

#define TERMOB_FAT_FILE_MAX_VISITED_CLUSTERS 256U
#define TERMOB_FAT_NAME_MAX 255U

typedef enum termob_fat_type {
    TERMOB_FAT_TYPE_NONE = 0,
    TERMOB_FAT_TYPE_12 = 12,
    TERMOB_FAT_TYPE_16 = 16,
    TERMOB_FAT_TYPE_32 = 32
} termob_fat_type_t;

typedef enum termob_fat_dirent_kind {
    TERMOB_FAT_DIRENT_FILE = 0,
    TERMOB_FAT_DIRENT_DIRECTORY,
    TERMOB_FAT_DIRENT_VOLUME_LABEL,
    TERMOB_FAT_DIRENT_LFN,
    TERMOB_FAT_DIRENT_DELETED,
    TERMOB_FAT_DIRENT_RESERVED
} termob_fat_dirent_kind_t;

typedef enum termob_fat_status {
    TERMOB_FAT_STATUS_OK = 0,
    TERMOB_FAT_STATUS_INVALID_ARGUMENT,
    TERMOB_FAT_STATUS_INVALID_PATH,
    TERMOB_FAT_STATUS_NOT_FOUND,
    TERMOB_FAT_STATUS_NOT_DIRECTORY,
    TERMOB_FAT_STATUS_NOT_FILE,
    TERMOB_FAT_STATUS_CORRUPT,
    TERMOB_FAT_STATUS_IO_ERROR
} termob_fat_status_t;

typedef struct termob_fat_fs {
    size_t device_index;
    uint32_t boot_lba;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t root_entry_count;
    uint32_t total_sectors;
    uint32_t first_fat_lba;
    uint32_t first_data_lba;
    uint32_t first_root_dir_lba;
    uint32_t root_dir_sectors;
    uint32_t root_cluster;
    uint32_t cluster_count;
    termob_fat_type_t fat_type;
} termob_fat_fs_t;

typedef struct termob_fat_dirent {
    char short_name[13];
    char display_name[TERMOB_FAT_NAME_MAX + 1U];
    uint8_t raw_name[11];
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t size_bytes;
    termob_fat_dirent_kind_t kind;
    uint8_t has_long_name;
    uint8_t suspicious;
} termob_fat_dirent_t;

typedef struct termob_fat_lookup_result {
    termob_fat_dirent_t dirent;
    uint8_t is_root;
} termob_fat_lookup_result_t;

typedef struct termob_fat_file {
    termob_fat_fs_t fs;
    termob_fat_dirent_t dirent;
    uint32_t position;
    uint32_t current_cluster;
    uint32_t current_cluster_file_offset;
    uint32_t clusters_seen;
    uint32_t visited_count;
    uint32_t visited_clusters[TERMOB_FAT_FILE_MAX_VISITED_CLUSTERS];
} termob_fat_file_t;

typedef int (*termob_fat_dirent_callback_t)(const termob_fat_fs_t* fs,
                                            const termob_fat_dirent_t* dirent,
                                            void* user);

const char* fat_type_name(termob_fat_type_t type);
const char* fat_dirent_kind_name(termob_fat_dirent_kind_t kind);
const char* fat_status_name(termob_fat_status_t status);

int fat_mount(termob_fat_fs_t* fs, size_t device_index, uint32_t boot_lba);
int fat_read_fat_entry(const termob_fat_fs_t* fs, uint32_t cluster, uint32_t* out_value);
uint32_t fat_cluster_first_lba(const termob_fat_fs_t* fs, uint32_t cluster);
termob_fat_status_t fat_lookup_path(const termob_fat_fs_t* fs,
                                    const char* path,
                                    termob_fat_lookup_result_t* out_result);
termob_fat_status_t fat_open_file(const termob_fat_fs_t* fs,
                                  const char* path,
                                  termob_fat_file_t* out_file);
termob_fat_status_t fat_seek_file(termob_fat_file_t* file, uint32_t offset);
termob_fat_status_t fat_read_file(termob_fat_file_t* file,
                                  void* buffer,
                                  size_t buffer_size,
                                  size_t* out_read_bytes);
termob_fat_status_t fat_read_file_range(const termob_fat_fs_t* fs,
                                        const char* path,
                                        uint32_t offset,
                                        void* buffer,
                                        size_t buffer_size,
                                        size_t* out_read_bytes);
termob_fat_status_t fat_list_path(const termob_fat_fs_t* fs,
                                  const char* path,
                                  termob_fat_dirent_callback_t callback,
                                  void* user);
int fat_list_directory(const termob_fat_fs_t* fs,
                       uint32_t start_cluster,
                       termob_fat_dirent_callback_t callback,
                       void* user);
int fat_list_root(const termob_fat_fs_t* fs,
                  termob_fat_dirent_callback_t callback,
                  void* user);

#endif
