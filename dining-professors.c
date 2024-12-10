/*
My solution for the famous Dining Professors problem 
which avoids deadlocks and starvation by getting help from Mutexes
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

pthread_mutex_t chopsticks[5];

struct professor {
    int id;
};

void* dine(void* params) {
    struct professor *args = (struct professor*) params;
    unsigned int professorID = args->id;

    while (1) {
        printf("Professor %d: thinking\n", professorID);
        sleep(rand() % 5 + 1);

        printf("Professor %d: trying to get left chopstick\n", professorID);
        pthread_mutex_lock(&chopsticks[professorID]);
        printf("Professor %d: got left chopstick\n", professorID);

        if (pthread_mutex_trylock(&chopsticks[(professorID + 1) % 5]) != 0) {
            printf("Professor %d: could not get right chopstick, putting down left chopstick\n", professorID);
            pthread_mutex_unlock(&chopsticks[professorID]); 
            sleep(rand() % 5 + 1); 
            continue;
        }

        printf("Professor %d: got both chopsticks, eating\n", professorID);
        sleep(rand() % 6 + 5);

        pthread_mutex_unlock(&chopsticks[(professorID + 1) % 5]);
        pthread_mutex_unlock(&chopsticks[professorID]);
        printf("Professor %d: finished eating and put down both chopsticks\n", professorID);
    }

    return NULL;
}

int main() {
    srand(time(NULL));
    pthread_t* professors = malloc(5 * sizeof(pthread_t));
    struct professor* args;

    for (int i = 0; i < 5; i++) {
        pthread_mutex_init(&chopsticks[i], NULL);
    }

    for (int i = 0; i < 5; i++) {
        args = malloc(sizeof(struct professor));
        args->id = i;
        pthread_create(&(professors[i]), NULL, dine, (void*)args);
    }

    for (int i = 0; i < 5; i++) {
        pthread_join(professors[i], NULL);
    }

    for (int i = 0; i < 5; i++) {
        pthread_mutex_destroy(&chopsticks[i]);
    }

    free(professors);

    return 0;
}
