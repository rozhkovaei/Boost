# Bayan

Utility to find duplicate files

## Command line options

Options:

    -h, --help         :  - Show this help
    -I, --include-dir  :  - Directories for scan (can be multiple)
    -E, --exclude-dir  :  - Directories excluded from scan (can be multiple)
    -L, --scan-level   :  - Scan level : 1 - all directories, 0 - without subdirs
    -F, --file-size    :  - Min file size included in scan (bytes)
    -M, --mask         :  - File name masks (can be multiple)
    -S, --block-size   :  - Reading block size (bytes)
    -H, --hash.        :  - Hash algorithm (supported md5 or crc32)
