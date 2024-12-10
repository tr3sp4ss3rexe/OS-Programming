/*
Simulating the physical memory and its paging process
using the FIFO algorithm and
counting the page faults
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int main(int argc, char** argv) {
    
    if (argc != 4) {
        printf("Wrong inputs. should be 3 arguments!");
        return 0;
    }

    unsigned int noPhysPages = atoi(argv[1]);
    unsigned int pageSize = atoi(argv[2]);
    char* fileName = argv[3];
    unsigned int front = 0, rear = 0, count = 0;
    unsigned int pageFaults = 0;
    unsigned int address, page;
    unsigned int references = 0;

    if (noPhysPages == 0 || pageSize == 0) {
        printf("page size or no physical pages cant be 0!");
        return 0;
    }

    printf("No physical pages = %u, page size = %u\n", noPhysPages, pageSize);
    printf("Reading memory trace from %s... \n", fileName);

    FILE* fp = fopen(fileName, "r");

    unsigned int* queue = malloc(noPhysPages * sizeof(unsigned int));

    while (fscanf(fp, "%u", &address) != EOF) {
        page = address / pageSize;
        int found = 0;

        for (unsigned int i = 0; i < count; i++) {
            unsigned int idx = (front + i) % noPhysPages;
            if (queue[idx] == page) {
                found = 1;
                break;
            }
        }

        if (!found) {
            pageFaults++;
            if (count < noPhysPages) {
                // Queue not full, add page at rear
                queue[rear] = page;
                rear = (rear + 1) % noPhysPages;
                count++;
            } else {
                // Queue full, replace the oldest page at front
                queue[front] = page;
                front = (front + 1) % noPhysPages;
                rear = (rear + 1) % noPhysPages;
            }
        }
        references++;
    }
    fclose(fp);

    printf("Read %u memory references\n", references);
    printf("Result: %u page faults\n", pageFaults);

    free(queue);
    return EXIT_SUCCESS;
}

