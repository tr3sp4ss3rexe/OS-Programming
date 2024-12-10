// Optimal page replacement algorithm 

#include <stdio.h>
#include <stdlib.h>

#define EMPTY_SLOT ((unsigned int)-1)

int findOptimal(int currentIndex, size_t totalRefs, unsigned int *refs, int numPages, unsigned int *pages) {
    int replaceIndex = -1, maxDistance = -1;

    for (int i = 0; i < numPages; i++) {
        int j;
        for (j = currentIndex + 1; j < totalRefs; j++) {
            if (pages[i] == refs[j]) {
                if (j > maxDistance) {
                    maxDistance = j;
                    replaceIndex = i;
                }
                break;
            }
        }
        if (j == totalRefs) return i;
    }
    return (replaceIndex == -1) ? 0 : replaceIndex;
}

int isPageInMemory(unsigned int *pages, int numPages, unsigned int page) {
    for (int i = 0; i < numPages; i++) {
        if (pages[i] == page) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s num_phys_pages page_size filename\n", argv[0]);
        return EXIT_FAILURE;
    }

    int numPages = atoi(argv[1]);
    unsigned int pageSize = atoi(argv[2]);
    char *filename = argv[3];

    if (numPages <= 0 || pageSize == 0) {
        fprintf(stderr, "Error: num_phys_pages and page_size must be positive integers.\n");
        return EXIT_FAILURE;
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }

    size_t capacity = 1000, totalRefs = 0;
    unsigned int *refs = malloc(capacity * sizeof(unsigned int));
    if (!refs) {
        perror("Error allocating memory");
        fclose(file);
        return EXIT_FAILURE;
    }

    unsigned int addr;
    while (fscanf(file, "%u", &addr) == 1) {
        if (totalRefs == capacity) {
            capacity *= 2;
            refs = realloc(refs, capacity * sizeof(unsigned int));
            if (!refs) {
                perror("Error reallocating memory");
                fclose(file);
                return EXIT_FAILURE;
            }
        }
        refs[totalRefs++] = addr / pageSize;
    }
    fclose(file);

    if (totalRefs == 0) {
        fprintf(stderr, "Error: No references found.\n");
        free(refs);
        return EXIT_FAILURE;
    }

    unsigned int *pages = malloc(numPages * sizeof(unsigned int));
    if (!pages) {
        perror("Error allocating memory for pages");
        free(refs);
        return EXIT_FAILURE;
    }
    for (int i = 0; i < numPages; i++) pages[i] = EMPTY_SLOT;

    // Simulate page replacement
    unsigned int pageFaults = 0;
    for (size_t i = 0; i < totalRefs; i++) {
        if (!isPageInMemory(pages, numPages, refs[i])) {
            pageFaults++;
            int replaceIndex = findOptimal(i, totalRefs, refs, numPages, pages);
            pages[replaceIndex] = refs[i];
        }
    }

    printf("Total references: %zu\n", totalRefs);
    printf("Page faults: %u\n", pageFaults);

    free(refs);
    free(pages);
    return EXIT_SUCCESS;
}
