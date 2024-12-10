/*
Simulating the physical memory and its paging process
using the Least Recently Used (LRU) algorithm and
counting the page faults
*/

#include <stdio.h>
#include <stdlib.h>

int findLRU(int* timestamps, int noPhysPages) {
    int min = timestamps[0], lruIndex = 0;
    for (int i = 1; i < noPhysPages; i++) {
        if (timestamps[i] < min) {
            min = timestamps[i];
            lruIndex = i;
        }
    }
    return lruIndex;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        printf("Wrong inputs. Should be 3 arguments!\n");
        return 0;
    }

    unsigned int noPhysPages = atoi(argv[1]);
    unsigned int pageSize = atoi(argv[2]);
    char* fileName = argv[3];

    if (noPhysPages == 0 || pageSize == 0) {
        printf("Page size or number of physical pages cannot be 0!\n");
        return 0;
    }

    printf("No physical pages = %u, page size = %u\n", noPhysPages, pageSize);
    printf("Reading memory trace from %s...\n", fileName);

    FILE* fp = fopen(fileName, "r");
    if (fp == NULL) {
        printf("Failed to open file: %s\n", fileName);
        return 0;
    }

    unsigned int* frames = malloc(noPhysPages * sizeof(unsigned int));
    int* timestamps = malloc(noPhysPages * sizeof(int));
    for (int i = 0; i < noPhysPages; i++) {
        frames[i] = -1;
        timestamps[i] = 0;
    }

    unsigned int address, page;
    unsigned int pageFaults = 0, references = 0;
    int time = 0;

    while (fscanf(fp, "%u", &address) != EOF) {
        page = address / pageSize;
        int found = 0;

        
        for (unsigned int i = 0; i < noPhysPages; i++) {
            if (frames[i] == page) {
                found = 1; // Page found
                timestamps[i] = time++; // Update the access time
                break;
            }
        }

        if (!found) {
            pageFaults++;
            int lruIndex = -1;

            if (references < noPhysPages) {
                lruIndex = references;
            } else {
                lruIndex = findLRU(timestamps, noPhysPages);
            }

            frames[lruIndex] = page;
            timestamps[lruIndex] = time++;
        }
        references++;
    }

    fclose(fp);

    printf("Read %u memory references\n", references);
    printf("Result: %u page faults\n", pageFaults);

    free(frames);
    free(timestamps);
    return 0;
}
