/*
Handles simple transactions and avoids race conditions 
with the help of mutexes when interacting with critical regions
*/


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
double bankAccountBalance = 0;
bool lock = 1;

void deposit(double amount) {
    pthread_mutex_lock(&lock);
    bankAccountBalance += amount;
    pthread_mutex_unlock(&lock);
}

void withdraw(double amount) {
    pthread_mutex_lock(&lock);
    bankAccountBalance -= amount;
    pthread_mutex_unlock(&lock);
}

unsigned odd(unsigned long num) {
    return num % 2;
}

void do1000Transactions(unsigned long id) {
    for (int i = 0; i < 1000; i++) {
        if (odd(id))
            deposit(100.00);
        else
            withdraw(100.00);
    }
}

void* child(void* buf) {
    unsigned long childID = (unsigned long)buf;
    do1000Transactions(childID);
    return NULL;    
}

int main(int argc, char** argv) {
    pthread_t *children;
    unsigned long id = 0;
    unsigned long nThreads = 0;
    if (argc > 1)
        nThreads = atoi(argv[1]);
    children = malloc( nThreads * sizeof(pthread_t) );
    for (id = 1; id < nThreads; id++)
        pthread_create(&(children[id-1]), NULL, child, (void*)id);
    do1000Transactions(0); // main thread work (id=0)
    for (id = 1; id < nThreads; id++)
        pthread_join(children[id-1], NULL);
    printf("\nThe final account balance with %lu threads is $%.2f.\n\n", nThreads, bankAccountBalance);
    free(children);
    pthread_mutex_destroy(&lock);
    return 0;
}
