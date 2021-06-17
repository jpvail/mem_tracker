#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h> 
#include <vector> 
#include <err.h>

#define PAGE_SIZE 0x1000

#define FIND_LIB_NAME

using std::vector; 

#define BIT_AT(val, x)	(((val) & (1ull << x)) != 0)
#define SET_BIT(val, x) ((val) | (1ull << x))

typedef unsigned long long u8;

/*
 * pfn to index of idle page file bitmap
 *
 * The bitmap should be read in 8 bytes (64 pages) stride.
 */
#define PFN_TO_IPF_IDX(pfn) pfn >> 6 << 3


static void print_page(uint64_t address, uint64_t data,
    const char *lib_name, vector<uint64_t> *vec) {

        uint64_t pfn = data & 0x7fffffffffffff; 
        if(pfn != 0){
            vec->push_back(pfn); 
        }
        
    /*
    printf("0x%-16lx : pfn %-16lx soft-dirty %ld file/shared %ld "
        "swapped %ld present %ld library %s\n",
        address,
        data & 0x7fffffffffffff,
        (data >> 55) & 1,
        (data >> 61) & 1,
        (data >> 62) & 1,
        (data >> 63) & 1,
        lib_name);
    */
}

void handle_virtual_range(int pagemap, uint64_t start_address,
    uint64_t end_address, const char *lib_name, vector<uint64_t> *vec) {

    for(uint64_t i = start_address; i < end_address; i += 0x1000) {
        uint64_t data;
        uint64_t index = (i / PAGE_SIZE) * sizeof(data);
        if(pread(pagemap, &data, sizeof(data), index) != sizeof(data)) {
            if(errno) perror("pread");
            break;
        }

        print_page(i, data, lib_name, vec);
    }
}

void parse_maps(const char *maps_file, const char *pagemap_file) {
    int maps = open(maps_file, O_RDONLY);
    if(maps < 0) return;

    int pagemap = open(pagemap_file, O_RDONLY);
    if(pagemap < 0) {
        close(maps);
        return;
    }

    char buffer[BUFSIZ];
    int offset = 0;

    vector<uint64_t> pfn_container; 

    for(;;) {
        ssize_t length = read(maps, buffer + offset, sizeof buffer - offset);
        if(length <= 0) break;

        length += offset;

        for(size_t i = offset; i < (size_t)length; i ++) {
            uint64_t low = 0, high = 0;
            if(buffer[i] == '\n' && i) {
                size_t x = i - 1;
                while(x && buffer[x] != '\n') x --;
                if(buffer[x] == '\n') x ++;
                size_t beginning = x;

                while(buffer[x] != '-' && x+1 < sizeof buffer) {
                    char c = buffer[x ++];
                    low *= 16;
                    if(c >= '0' && c <= '9') {
                        low += c - '0';
                    }
                    else if(c >= 'a' && c <= 'f') {
                        low += c - 'a' + 10;
                    }
                    else break;
                }

                while(buffer[x] != '-' && x+1 < sizeof buffer) x ++;
                if(buffer[x] == '-') x ++;

                while(buffer[x] != ' ' && x+1 < sizeof buffer) {
                    char c = buffer[x ++];
                    high *= 16;
                    if(c >= '0' && c <= '9') {
                        high += c - '0';
                    }
                    else if(c >= 'a' && c <= 'f') {
                        high += c - 'a' + 10;
                    }
                    else break;
                }

                const char *lib_name = 0;
#ifdef FIND_LIB_NAME
                for(int field = 0; field < 4; field ++) {
                    x ++;  // skip space
                    while(buffer[x] != ' ' && x+1 < sizeof buffer) x ++;
                }
                while(buffer[x] == ' ' && x+1 < sizeof buffer) x ++;

                size_t y = x;
                while(buffer[y] != '\n' && y+1 < sizeof buffer) y ++;
                buffer[y] = 0;

                lib_name = buffer + x;
#endif

                handle_virtual_range(pagemap, low, high, lib_name, &pfn_container);

#ifdef FIND_LIB_NAME
                buffer[y] = '\n';
#endif
            }
        }
    }

    //pfn_container populated w all PFNs 

    int fd = open("/sys/kernel/mm/page_idle/bitmap", O_RDWR);
    
    if (fd < 0)
            err(2, "open bitmap");

    uint64_t pfn, entry;
    uint64_t i;

    for(;;){
        for(i = 0; i < pfn_container.size(); ++i){
            pfn = pfn_container[i];
            entry = 0;
            if (pread(fd, &entry, sizeof(entry), pfn / 64 * 8)
                    != sizeof(entry))
                err(2, "%s: read bitmap", __func__);
            entry = SET_BIT(entry, pfn % 64);
            if (pwrite(fd, &entry, sizeof(entry), pfn / 64 * 8)
                    != sizeof(entry))
                err(2, "%s: write bitmap", __func__);
        }

        //check if accessed 
        for(i = 0; i < pfn_container.size(); ++i){
            pfn = pfn_container[i];
            entry = 0;
            if (pread(fd, &entry, sizeof(entry), PFN_TO_IPF_IDX(pfn))
                    != sizeof(entry))
                err(2, "%s: read bitmap", __func__);
            printf("%d ", (int)BIT_AT(entry, pfn % 64));

            if((int)BIT_AT(entry, pfn % 64) == 0){
                printf("%p \n", pfn); 
            }
        }

        close(fd);
    }

    close(maps);
    close(pagemap);
}


void process_pid(pid_t pid) {
    char maps_file[BUFSIZ];
    char pagemap_file[BUFSIZ];
    snprintf(maps_file, sizeof(maps_file),
        "/proc/%lu/maps", (uint64_t)pid);
    snprintf(pagemap_file, sizeof(pagemap_file),
        "/proc/%lu/pagemap", (uint64_t)pid);

    parse_maps(maps_file, pagemap_file);
}

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: %s pid1 [pid2...]\n", argv[0]);
        return 1;
    }

    for(int i = 1; i < argc; i ++) {
        pid_t pid = (pid_t)strtoul(argv[i], NULL, 0);

        printf("=== Maps for pid %d\n", (int)pid);
        process_pid(pid);
    }

    return 0;
}

