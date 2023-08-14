#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "LinkedList.h"

#define BUFFER_SIZE 257

extern int total_guesses;
extern int total_wins;
extern int total_losses;
extern char **words;
int words_size = 1;

// I hate threads.
bool server_shutdown = false;
bool signalled = false;
struct List *global_thread_list;

pthread_mutex_t mutex_words = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_losses = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_wins = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_guesses = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_list = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_targs = PTHREAD_MUTEX_INITIALIZER;

struct args {
    int csd;
    int dict_len;
    char **dictionary;
    struct List *thread_list;
};

int badInput() {
    fprintf(stderr,
            "ERROR: Invalid argument(s)\nUSAGE: hw3.out <listener-port> <seed> "
            "<dictionary-filename> <num-words>\n");
    return EXIT_FAILURE;
}

// This is called if the server encounters an error and would otherwise shut
// down. Cleans up all dynamic memory allocated before the server goes live.
void cleanupServer(char **dictionary, int dictsz, struct args *thread_arguments,
                   struct List *thread_list) {
    // This function is only called from main, so
    //  First we wait for all thread activity to stop
    server_shutdown = true;
    signalled = true;
    int running = -1;
    do {
        pthread_mutex_lock(&mutex_list);
        { running = thread_list->size; }
        pthread_mutex_unlock(&mutex_list);
    } while (running != 0);

    // Now that we know no threads are using this memory,
    //  we can free it up.
    for (int i = 0; i < dictsz; i++) {
        free(*(dictionary + i));
    }

    free(dictionary);
    free(thread_arguments);
    free(thread_list);
}

// Only called if the server recieves SIGUSR1
// In theory this ensures that there are no running threads once this
//  handler returns.
void killServer(int sig) {
    // should solicit threads to stop execution at a safe place.
    if (sig == SIGUSR1) {
        server_shutdown = true;
        int running = -1;

        do {
            pthread_mutex_lock(&mutex_list);
            running = global_thread_list->size;
            pthread_mutex_unlock(&mutex_list);
        } while (running != 0);
    }
}

// we do a bit of lowercasing
void strlower(char *str) {
    for (int i = 0; i < strlen(str); i++) {
        *(str + i) = tolower(*(str + i));
    }
}

// Compares the guess given by the client to the wordle being played against
//  according to the wordle algorithm. Returns a string in result where correct
//  letter in correct position is capitalized, correct letter in the wrong
//  position is lowercase in the guess position, and wrong letter is just a '-'
// Expects that all 3 arguments are character strings of length 5 (not including
//  null terminator)
// If either wordle or guess point to the same heap memory as result, the state
//  of all 3 strings after the call is undefined.
void evaluateWordleGuess(const char *wordle, const char *guess, char *result) {
    if (wordle == NULL || guess == NULL || result == NULL) {
        fprintf(stderr, "wordle() failed: NULL pointer arguments\n");
        return;
    }

    int i, j;
    bool *wordleUsed = calloc(5, sizeof(bool));
    bool *guessUsed = calloc(5, sizeof(bool));

    // Correct letter in correct position
    for (i = 0; i < 5; i++) {
        if (*(wordle + i) == *(guess + i)) {
            *(result + i) = toupper(*(guess + i));
            *(wordleUsed + i) = true;
            *(guessUsed + i) = true;
        }
    }

    // Check for yellow feedback
    for (i = 0; i < 5; i++) {
        if (!*(guessUsed + i)) {
            for (j = 0; j < 5; j++) {
                if (!*(wordleUsed + j) && *(wordle + j) == *(guess + i)) {
                    *(result + i) = *(guess + i);
                    *(wordleUsed + j) = true;
                    *(guessUsed + i) = true;
                    break;
                }
            }
        }
    }

    // Mark the rest as gray
    for (i = 0; i < 5; i++) {
        if (!*(guessUsed + i)) {
            *(result + i) = '-';
        }
    }
}

// This function just parses the dictionary file.
// Returns EXIT_FAILURE on error and EXIT_SUCCESS otherwise
int readDict(FILE *dict_in, char **dict, int dict_size) {
    char *word_buffer = (char *)calloc(BUFFER_SIZE, sizeof(char));

    for (int i = 0; i < dict_size; i++) {
        int numread = fscanf(dict_in, "%s", word_buffer);
        if (numread == 0) {
            fprintf(stderr, "ERROR: Failed to read before EOF\n");
            return EXIT_FAILURE;
        }
        if (numread == EOF) {
            perror("ERROR: fscanf() failed");
            return EXIT_FAILURE;
        }
        // All words in the dictionary should only be this long...
        *(dict + i) = calloc(6, sizeof(char));
        if (*(dict + i) == NULL) {
            fprintf(stderr, "ERROR: calloc() failed\n");
            return EXIT_FAILURE;
        }

        strcpy(*(dict + i), word_buffer);
        // case is irrelevant, so lower everything.
        strlower(*(dict + i));

        // Better than worrying about leftover data...
        memset(word_buffer, 0, strlen(word_buffer));
    }

    // this is only needed for dictionary population
    free(word_buffer);
    return EXIT_SUCCESS;
}

void *do_on_thread(void *arguments) {
    // This conversion is implicit but im putting it here anyway
    struct args *thread_args = (struct args *)arguments;
    int csd = thread_args->csd;
    char **dict = thread_args->dictionary;
    int dict_sz = thread_args->dict_len;
    char **tmp_words;
    struct List *running_threads = thread_args->thread_list;
    // which word from the dictionary is our game played against?
    int dict_index = rand() % dict_sz;

    char *wordle = calloc(6, sizeof(char));
    if (wordle == NULL) {
        fprintf(stderr, "THREAD %lu: ERROR: failed to allocate wordle",
                pthread_self());
        pthread_mutex_lock(&mutex_list);
        { removeList(running_threads, pthread_self()); }
        pthread_mutex_unlock(&mutex_list);
        pthread_exit(NULL);
    }

    strcpy(wordle, *(dict + dict_index));
#ifdef BAD_AT_THIS
    printf("THREAD %lu: wordle is: %s\n", pthread_self(), wordle);
#endif
    // We have our word, we can now add it to the global set of words used.
    pthread_mutex_lock(&mutex_words);
    {
        tmp_words = realloc(words, sizeof(char *) * (words_size + 1));
        if (tmp_words != NULL) {
            // TODO: Pretty sure this is a problem, I think i have to allocate
            // lhs first...
            *(words + words_size - 1) = calloc(1, sizeof(char *));
            strcpy(*(words + words_size - 1), wordle);
            *(tmp_words + words_size) = NULL;
            words_size++;
            words = tmp_words;
        } else {
            server_shutdown = true;
        }
    }
    pthread_mutex_unlock(&mutex_words);

    // Checking this variable after every mutex, this is a better alternative to
    // signals, since I dont need to worry about whether a thread currently
    // holds a mutex
    if (server_shutdown) {

        free(wordle);

        pthread_mutex_lock(&mutex_list);
        { removeList(running_threads, pthread_self()); }
        pthread_mutex_unlock(&mutex_list);
        pthread_exit(NULL);
    }

    uint16_t guesses_remaining = 6;

    int bytes_sent;
    int bytes_recieved;

    // Because TCP is a stream protocol.
    char buff_buffer;

    char *recv_buffer = calloc(6, sizeof(char));
    if (recv_buffer == NULL) {
        fprintf(stderr, "THREAD %lu: ERROR: calloc() on recv_buffer failed\n",
                pthread_self());

        free(wordle);

        pthread_mutex_lock(&mutex_list);
        { removeList(running_threads, pthread_self()); }
        pthread_mutex_unlock(&mutex_list);
        pthread_exit(NULL);
    }
    char *send_buffer = calloc(9, sizeof(char));
    if (send_buffer == NULL) {
        fprintf(stderr, "THREAD %lu: ERROR: calloc() on send_buffer failed\n",
                pthread_self());

        free(wordle);
        free(recv_buffer);

        pthread_mutex_lock(&mutex_list);
        { removeList(running_threads, pthread_self()); }
        pthread_mutex_unlock(&mutex_list);
        pthread_exit(NULL);
    }

    // For guess validation
    bool valid, winner = false;

    short net_short;
    while (guesses_remaining > 0 && !winner) {
        // First thing we are doing is checking if we have been told to stop.
        // So when the server shuts down, it will finish what it is doing
        //  and then stop before it would have accepted new input.

        printf("THREAD %lu: waiting for guess\n", pthread_self());

        bytes_recieved = recv(csd, recv_buffer, 6, 0);

        if (bytes_recieved == -1) {
            perror("ERROR: recv() failed");

            free(wordle);
            free(recv_buffer);
            free(send_buffer);

            pthread_mutex_lock(&mutex_list);
            { removeList(running_threads, pthread_self()); }
            pthread_mutex_unlock(&mutex_list);
            pthread_exit(NULL);

        } else if (bytes_recieved == 0) {
            printf("THREAD %lu: client gave up; closing TCP connection...\n",
                   pthread_self());
            // client disconnected. mark a loss and kill the connection
            pthread_mutex_lock(&mutex_losses);
            { total_losses++; }
            pthread_mutex_unlock(&mutex_losses);

            free(wordle);
            free(recv_buffer);
            free(send_buffer);

            pthread_mutex_lock(&mutex_list);
            { removeList(running_threads, pthread_self()); }
            pthread_mutex_unlock(&mutex_list);
            pthread_exit(NULL);
        } else if (bytes_recieved < 5) {
            // Wait for the remaining number of bytes.......
            while (strlen(recv_buffer) < 5) {
                if (recv(csd, &buff_buffer, 1, 0) == -1) {
                    perror("ERROR: recv() failed");

                    free(wordle);
                    free(recv_buffer);
                    free(send_buffer);

                    pthread_mutex_lock(&mutex_list);
                    { removeList(running_threads, pthread_self()); }
                    pthread_mutex_unlock(&mutex_list);
                    pthread_exit(NULL);
                }

                strncat(recv_buffer, &buff_buffer, 1);
                *(recv_buffer + 5) = '\0';
            }
        }

        // I know how long the string is at this point
        *(recv_buffer + 5) = '\0';

        strlower(recv_buffer);

        printf("THREAD %lu: rcvd guess: %s\n", pthread_self(), recv_buffer);

        // check if our guess is in the dictionary
        // We can skip this if we recieved an incorrect number of bytes
        // Since the guess is automatically invalid.
        valid = false;

        if (bytes_recieved == 5) {
            for (int i = 0; i < dict_sz; i++) {
                if (strcmp(*(dict + i), recv_buffer) == 0) {
                    valid = true;
                    break;
                }
            }
        }

        if (!valid) {
            // Send an invalid guess response
            printf("THREAD %lu: invalid guess; sending reply: ????? (%hd "
                   "guess%s left)\n",
                   pthread_self(), guesses_remaining,
                   (guesses_remaining == 1 ? "" : "es"));
            sprintf(send_buffer, "N%hd?????", htons(guesses_remaining));
            bytes_sent = send(csd, send_buffer, strlen(send_buffer), 0);

            if (bytes_sent == -1) {
                perror("ERROR: send() failed");

                free(wordle);
                free(recv_buffer);
                free(send_buffer);

                pthread_mutex_lock(&mutex_list);
                { removeList(running_threads, pthread_self()); }
                pthread_mutex_unlock(&mutex_list);

                pthread_exit(NULL);
            }

            continue;
        }

        pthread_mutex_lock(&mutex_guesses);
        { total_guesses++; }
        pthread_mutex_unlock(&mutex_guesses);

        --guesses_remaining;

        // Ensure the buffer is in the same state for every iteration.
        memset(send_buffer, 0, 9);

        if (strcmp(wordle, recv_buffer) == 0) {
            winner = true;
        }

        evaluateWordleGuess(wordle, recv_buffer, send_buffer + 3);

        memset(send_buffer, 'Y', 1);

        net_short = htons(guesses_remaining);
        memcpy(send_buffer + 1, &net_short, sizeof(short));
        // *(short *)(send_buffer + 1) = htons(guesses_remaining);

        // I guess I'm manually setting the bytes of the send buffer, since no
        // other method of encoding the guesses remaining worked.
        // *(send_buffer + 1) = (net_short >> 8) & 0xFF; // High
        // *(send_buffer + 2) = net_short & 0xFF;        // Low

#ifdef BAD_AT_THIS
        printf("THREAD %lu: contents of send buffer after validation:",
               pthread_self());
        for (int i = 0; i < 9; i++) {
            printf(" %02x |", *(send_buffer + i));
        }
        printf("\n");

        printf("THREAD %lu: valid: %c  guesses: %02x %02x | %hd reply: %s\n",
               pthread_self(), *send_buffer, *(send_buffer + 1),
               *(send_buffer + 2), ntohs(*(short *)(send_buffer + 1)),
               send_buffer + 3);
#endif

        // Now we can send a response to the client.
        printf("THREAD %lu: sending reply: %s (%d guess%s left)\n",
               pthread_self(), send_buffer + 3, guesses_remaining,
               (guesses_remaining == 1 ? "" : "es"));
        bytes_sent = send(csd, send_buffer, 9, 0);

        if (bytes_sent == -1) {
            perror("ERROR: send() failed");

            free(wordle);
            free(recv_buffer);
            free(send_buffer);

            pthread_mutex_lock(&mutex_list);
            { removeList(running_threads, pthread_self()); }
            pthread_mutex_unlock(&mutex_list);

            pthread_exit(NULL);
        }
    }
    // Checking this one more time before just letting the thread finish.
    if (server_shutdown) {
        free(wordle);
        free(recv_buffer);
        free(send_buffer);

        pthread_mutex_lock(&mutex_list);
        { removeList(running_threads, pthread_self()); }
        pthread_mutex_unlock(&mutex_list);
        pthread_exit(NULL);
    }

    if (winner) {
        pthread_mutex_lock(&mutex_wins);
        { total_wins++; }
        pthread_mutex_unlock(&mutex_wins);
    } else {
        pthread_mutex_lock(&mutex_losses);
        { total_losses++; }
        pthread_mutex_unlock(&mutex_losses);
    }
    // Going to do some shenanigans to make this print work
    for (int i = 0; i < strlen(wordle); i++) {
        *(wordle + i) = toupper(*(wordle + i));
    }
    printf("THREAD %lu: game over; word was %s!\n", pthread_self(), wordle);

    free(wordle);
    free(recv_buffer);
    free(send_buffer);

    pthread_mutex_lock(&mutex_list);
    { removeList(running_threads, pthread_self()); }
    pthread_mutex_unlock(&mutex_list);

    pthread_exit(NULL);
}

// TODO: fix the signal handler, but only if I really have to :>
int wordle_server(int argc, char **argv) {
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    // signal(SIGUSR1, killServer);

    if (argc != 5) {
        return badInput();
    }

    unsigned short tcp_port;
    if (sscanf(*(argv + 1), "%hd", &tcp_port) == EOF) {
        return badInput();
    }

    unsigned int seed;
    if (sscanf(*(argv + 2), "%u", &seed) == EOF) {
        return badInput();
    }

    char *dict_fn = calloc(BUFFER_SIZE, sizeof(char));
    if (dict_fn == NULL) {
        fprintf(stderr, "ERROR: calloc() failed\n");
        return EXIT_FAILURE;
    }

    if (sscanf(*(argv + 3), "%s", dict_fn) == EOF) {
        return badInput();
    }

    int dict_size;
    if (sscanf(*(argv + 4), "%d", &dict_size) == EOF) {
        return badInput();
    }
    char **dict = calloc(dict_size, sizeof(char *));

    FILE *dict_in = fopen(dict_fn, "r");
    if (dict_in == NULL) {
        perror("ERROR: open() failed");
        return EXIT_FAILURE;
    }

    printf("MAIN: opened %s (%d words)\n", dict_fn, dict_size);

    free(dict_fn);

    // populate our dictionary
    if (readDict(dict_in, dict, dict_size) != 0) {
        // I cant make any guarantees about the state of dict here.
        // Better off just stopping and letting the system clean it up. :<
        // This is a bad way to do this
        return EXIT_FAILURE;
    }

#ifdef BAD_AT_THIS
    printf("MAIN: Successfully populated dictionary.\n");
#endif

    srand(seed);
    printf("MAIN: seeded pseudo-random number generator with %d\n", seed);

    // Start server setup

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == -1) {
        perror("ERROR: socket() failed");
        return EXIT_FAILURE;
    }

    printf("MAIN: Wordle server listening on port {%d}\n", tcp_port);

    // populating socket structure
    struct sockaddr_in tcp_server;
    tcp_server.sin_family = AF_INET;

// not sure if I'm allowing any address, but they haven't told me otherwise
// so...
#ifdef LOCAL_HOST
    tcp_server.sin_addr.s_addr = inet_addr("127.0.0.1");
#else
    tcp_server.sin_addr.s_addr = htonl(INADDR_ANY);
#endif
    tcp_server.sin_port = htons(tcp_port);

    if (bind(listener, (struct sockaddr *)&tcp_server, sizeof(tcp_server)) ==
        -1) {
        perror("ERROR: bind() failed");
        return EXIT_FAILURE;
    }

    // be prepared to have to change this 5. there is nothing about it in the
    // pdf...
    if (listen(listener, 5) == -1) {
        perror("listen() failed");
        return EXIT_FAILURE;
    }
    // Presumably the rest of this is application protocol

    struct sockaddr_in remote_client;
    int addrlen = sizeof(remote_client);
    int rc;

    struct args *thread_args = calloc(1, sizeof(struct args));

    // Initialize the list...
    struct List *current_threads = newList();
    global_thread_list = current_threads;
    int sd;
    pthread_t new_thread;

    // Dont accept any new connections if the server has been killed,
    // if the server is signaled in the middle of a loop be
    // any new threads created will terminate without taking input.
    while (!server_shutdown) {

        // Prepare for the next client's socket descriptor...
        // Block on accept
        sd = accept(listener, (struct sockaddr *)&remote_client,
                    (socklen_t *)&addrlen);
        if (sd == -1) {
            perror("ERROR: accept() failed");

            cleanupServer(dict, dict_size, thread_args, current_threads);
            return EXIT_FAILURE;
        }

        printf("MAIN: rcvd incoming connection request\n");

        pthread_mutex_lock(&mutex_targs);
        {
            thread_args->csd = sd;
            thread_args->dictionary = dict;
            thread_args->dict_len = dict_size;
            thread_args->thread_list = current_threads;
        }
        pthread_mutex_unlock(&mutex_targs);

        if (server_shutdown) {
            if (signalled)
                printf("MAIN: SIGUSR1 rcvd; Wordle server shutting down...\n");

            cleanupServer(dict, dict_size, thread_args, current_threads);
            return EXIT_SUCCESS;
        }

        rc = pthread_create(&new_thread, NULL, do_on_thread, thread_args);

        if (rc != 0) {
            fprintf(stderr, "ERROR: pthread_create() failed with code: %d\n",
                    rc);

            cleanupServer(dict, dict_size, thread_args, current_threads);
            return EXIT_FAILURE;
        }
        // Finally, detach the thread so we dont need to join it anymore.
        if (pthread_detach(new_thread) != 0) {
            fprintf(stderr, "ERROR: pthread_detach failed()\n");

            cleanupServer(dict, dict_size, thread_args, current_threads);
            return EXIT_FAILURE;
        }

        // Threads are allowed to remove themselves from the list on
        //  termination, so a mutex is necessary.
        pthread_mutex_lock(&mutex_list);
        { push_back(current_threads, sd, new_thread); }
        pthread_mutex_unlock(&mutex_list);
    }

    // (server_shutdown == true);
    if (signalled)
        printf("MAIN: SIGUSR1 rcvd; Wordle server shutting down...\n");
    cleanupServer(dict, dict_size, thread_args, current_threads);
    return EXIT_SUCCESS;
}
