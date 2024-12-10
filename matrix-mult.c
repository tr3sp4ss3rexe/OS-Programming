/*
Simple example of a huge matrix multiplication
and speeding the process up as much as possible
by running the main tasks parallelly using threads
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#define SIZE 1024

static double a[SIZE][SIZE];
static double b[SIZE][SIZE];
static double c[SIZE][SIZE];

void *initialize_row(void *arg) {
    int row = *(int *)arg;
    free(arg);

    for (int j = 0; j < SIZE; j++) {
        a[row][j] = 1.0;
        b[row][j] = 1.0;
    }
    return NULL;
}

static void init_matrix() {
    pthread_t threads[SIZE];

    for (int i = 0; i < SIZE; i++) {
        int *row = malloc(sizeof(int));
        *row = i;

        pthread_create(&threads[i], NULL, initialize_row, row);
    }

    for (int i = 0; i < SIZE; i++) {
        pthread_join(threads[i], NULL);
    }
}


static void
print_matrix(void)
{
    int i, j;

    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++)
	        printf(" %7.2f", c[i][j]);
	    printf("\n");
    }
}


// using the pthread to make it run parallel 
void *matmul_rows(void *arg) {
    int row = *(int *)arg;
    free(arg);

    for (int j = 0; j < SIZE; j++) {
        c[row][j] = 0.0;
        for (int k = 0; k < SIZE; k++) {
            c[row][j] += a[row][k] * b[k][j];
        }
    }
    return NULL;
}

static void matmul_para() {
    pthread_t threads[SIZE];

    for (int i = 0; i < SIZE; i++) {
        int *row = malloc(sizeof(int));
        *row = i;

        pthread_create(&threads[i], NULL, matmul_rows, row);
    }

    for (int i = 0; i < SIZE; i++) {
        pthread_join(threads[i], NULL);
    }
}


int
main(int argc, char **argv)
{
    init_matrix();
    matmul_para();
    print_matrix();
}
