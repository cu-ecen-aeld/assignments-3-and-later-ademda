#define _POSIX_C_SOURCE 200112L
#define _DEFAULT_SOURCE

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <syslog.h>
#include <time.h>

#include <unistd.h>
#include <string.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <signal.h>

#include <pthread.h>
#include <sys/queue.h>

#ifndef USE_AESD_CHAR_DEVICE
    #define USE_AESD_CHAR_DEVICE 1
#endif

#define LISTEN_BACKLOG 64
#define BUFFER_SIZE 64

#if USE_AESD_CHAR_DEVICE == 1
    #define TMP_PATH "/dev/aesdchar"
#else
    #define TMP_PATH "/var/tmp/aesdsocketdata"
#endif

SLIST_HEAD(thread_list_t, thread_list_entry) thread_list_head = SLIST_HEAD_INITIALIZER(thread_list_head);

struct thread_list_entry {
    struct thread_args *args;
    pthread_t thread;
    SLIST_ENTRY(thread_list_entry) entries;
};

static bool shutdown_signal = false;
static pthread_mutex_t file_mutex;

struct thread_args {
    int done;
    int result;
    int server_fd;
    struct sockaddr addr;
};

static void signal_handler(int signal_number) {
    shutdown_signal = true;
}

#if USE_AESD_CHAR_DEVICE != 1
    static void timer_handler(union sigval sigval) {
        if(pthread_mutex_lock(&file_mutex) != 0) {
            syslog(LOG_ERR, "Mutex lock failed");
            return;
        }

        FILE *output_file = fopen(TMP_PATH, "a+");
        if(output_file == NULL) {
            syslog(LOG_ERR, "Cannot open output file");
            exit(-1);
        }

        char outstr[256] = "timestamp:";
        size_t outstr_target_len = sizeof(outstr) - strlen(outstr);
        char* outstr_target = outstr + strlen(outstr);

        time_t t;
        struct tm *tmp;

        t = time(NULL);
        tmp = localtime(&t);
        if(tmp == NULL) {
            syslog(LOG_ERR, "Getting time failed");
        } else if (strftime(outstr_target, outstr_target_len, "%a, %d %b %Y %T %z\n", tmp) == 0) {
            syslog(LOG_ERR, "Getting time failed, strftime");
        } else if(fputs(outstr, output_file) < 0) {
            syslog(LOG_ERR, "Getting time failed, strftime");
        }

        fflush(output_file);
        fclose(output_file);
        
        if(pthread_mutex_unlock(&file_mutex)) {
            syslog(LOG_ERR, "Mutex unlock failed");
        }
        return;
    }
#endif

static int setup_server(bool is_daemon) {
    assert(sizeof(unsigned char) == 1);

    openlog(NULL, 0, LOG_USER);

    SLIST_INIT(&thread_list_head);

    if(pthread_mutex_init(&file_mutex, NULL) != 0) {
        syslog(LOG_ERR, "Cannot create mutex");
        exit(-1);
    }

    // setup signal hander for interruption
    struct sigaction sig_act;
    memset(&sig_act, 0, sizeof(sig_act));
    sig_act.sa_handler = signal_handler;
    if(sigaction(SIGTERM, &sig_act, NULL) || sigaction(SIGINT, &sig_act, NULL)) {
        syslog(LOG_ERR, "Cannot register for termination signal handlers");
        exit(-1);
    }

    // open socket to port 9000; exit with -1 if fails
    int socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    if(socket_fd < 0) {
        syslog(LOG_ERR, "Could not open socket");
        exit(-1);
    }

    struct addrinfo hints;
    struct addrinfo *servinfo;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    
    if(getaddrinfo(NULL, "9000", &hints, &servinfo)) {
        exit(-1);
    }

    int reuseaddr = 1;
    if(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) == -1) {
        syslog(LOG_ERR, "Could not set socket option");
        exit(-1);
    }

    if(bind(socket_fd, servinfo->ai_addr, servinfo->ai_addrlen)) {
        syslog(LOG_ERR, "Could not bind:");
        exit(-1);
    }

    free(servinfo);

    if(is_daemon) {
        if(daemon(0,0) == -1) {
            syslog(LOG_ERR, "Could not fork daemon");
            fprintf(stderr, "Could not fork daemon");
            exit(-1);
        }
    }

    #if USE_AESD_CHAR_DEVICE != 1
        // setup signal handler for timer
        struct sigevent sev;
        timer_t timerid;
        memset(&sev, 0, sizeof(sev));
        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_notify_function = &timer_handler;
        if(timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1) {
            syslog(LOG_ERR, "Cannot register for timer signal handler");
            exit(-1);
        }

        // actually start timer
        struct itimerspec its;
        its.it_value.tv_sec = 10;
        its.it_value.tv_nsec = 0;
        its.it_interval.tv_sec = its.it_value.tv_sec;
        its.it_interval.tv_nsec = its.it_value.tv_nsec;
        if (timer_settime(timerid, 0, &its, NULL) == -1) {
            syslog(LOG_ERR, "Could not start timer.");
            exit(-1);
        }
    #endif

    return socket_fd;
}

static int ip_to_str(struct sockaddr *addr, char* target){
    struct sockaddr_in *addr_in = (struct sockaddr_in*) addr;
    if(inet_ntop(AF_INET, &(addr_in->sin_addr), target, INET_ADDRSTRLEN) == NULL) {
        syslog(LOG_ERR, "Could not convert ip to string");
        return -1;
    }
    return 0;
}

static int socket_to_file(unsigned char* buffer, size_t buffer_size, FILE* output_file, int server_fd) {
    ssize_t bytes_received;
    do {
        bytes_received = read(server_fd, buffer, buffer_size);
        if(bytes_received < 0) {
            syslog(LOG_ERR, "Reading from socket failed: %s", strerror(errno));
            return -1;
        }
        ssize_t bytes_written = fwrite(buffer, 1, bytes_received, output_file);
        if(bytes_written < bytes_received) {
            syslog(LOG_ERR, "Could not write received bytes to buffer");
            return -1;
        }
    } while(memchr(buffer, '\n', bytes_received) == NULL);
    assert(buffer[bytes_received - 1] == '\n'); // check assumption: last char is \n
    return 0;
}

static int file_to_socket(unsigned char* buffer, size_t buffer_size, FILE* file, int server_fd) {
    rewind(file);
    do {
        ssize_t bytes_remaining = fread(buffer, 1, buffer_size, file);
        ssize_t total_received = 0;
        while(bytes_remaining > 0) {
            ssize_t written = write(server_fd, buffer + total_received, bytes_remaining);
            if(written == -1) {
                syslog(LOG_ERR, "Writing of data failed");
                return -1;
            }
            total_received += written;
            bytes_remaining -= written;
        }
    } while(feof(file) == 0);
    return 0;
}

static int run_thread_handler(struct thread_args *args) {
    char ipAddrStr[INET_ADDRSTRLEN];
    unsigned char buffer[BUFFER_SIZE];

    if(ip_to_str(&args->addr, ipAddrStr) != 0) {
        return -1;
    }
    syslog(LOG_DEBUG, "Accepted connection from %s", ipAddrStr);

    if(pthread_mutex_lock(&file_mutex) != 0) {
        syslog(LOG_ERR, "Mutex lock failed");
        return -1;
    }

    FILE *output_file = fopen(TMP_PATH, "a+");
    if(output_file == NULL) {
        syslog(LOG_ERR, "Cannot open output file");
        exit(-1);
    }

    clearerr(output_file);
    if(socket_to_file(buffer, BUFFER_SIZE, output_file, args->server_fd) != 0) {
        return -1;
    }

    fflush(output_file);
    if(file_to_socket(buffer, BUFFER_SIZE, output_file, args->server_fd) != 0) {
        return -1;
    }

    fclose(output_file);

    if(pthread_mutex_unlock(&file_mutex)) {
        syslog(LOG_ERR, "Mutex unlock failed");
        return -1;
    }

    // syslog: Closed connection from $IP
    syslog(LOG_DEBUG, "Closed connection from %s", ipAddrStr);

    return 0;
}

static void* run_thread(void* voidp_args) {
    struct thread_args *args = (struct thread_args*) voidp_args;
    args->done = 0;
    args->result = run_thread_handler(args);
    args->done = 1;
    pthread_exit(NULL);
}

int main(int argc, char** argv) {
    if(argc > 2) {
        fprintf(stderr, "Usage: %s [-d]", argv[0]);
        exit(-1);
    }

    bool is_daemon = false;
    if(argc == 2) {
        if(strncmp(argv[1], "-d", 3) == 0) {
            is_daemon = true;
        } else {
            fprintf(stderr, "Usage: %s [-d]", argv[0]);
            exit(-1);
        }
    }

    int socket_fd = setup_server(is_daemon);
    if(socket_fd < 0) {
        exit(-1);
    }

    while(!shutdown_signal) {
        // Listen + Accept connection
        if(listen(socket_fd, LISTEN_BACKLOG)) {
            syslog(LOG_ERR, "Could not listen to socket");
            return -1;
        }

        struct thread_args *targs = malloc(sizeof(struct thread_args));
        if(!targs) {
            syslog(LOG_ERR, "Malloc failed");
            exit(-1);
        }

        socklen_t addrlen = sizeof(targs->addr);
        targs->server_fd = accept(socket_fd, &targs->addr, &addrlen);
        targs->done = 0;
        if(targs->server_fd < 0) {
            syslog(LOG_ERR, "Could not accept connection");
            free(targs);
            continue;
        }

        pthread_t tid;
        if(pthread_create(&tid, NULL, run_thread, targs) != 0) {
            syslog(LOG_ERR, "Cannot create handler thread");
            free(targs);
            close(targs->server_fd);
            continue;
        }

        struct thread_list_entry *t = malloc(sizeof(struct thread_list_entry));
        if(!t) {
            syslog(LOG_ERR, "Malloc failed");
            exit(-1);
        }
        t->args = targs;
        t->thread = tid;
        SLIST_INSERT_HEAD(&thread_list_head, t, entries);

        // cleanup thread list
        struct thread_list_entry *t1, *t2;
        t1 = SLIST_FIRST(&thread_list_head);
        while(t1 != NULL) {
            t2 = t1;
            t1 = SLIST_NEXT(t1, entries);

            if(t2->args->done == 1) {
                close(t2->args->server_fd);
                SLIST_REMOVE(&thread_list_head, t2, thread_list_entry, entries);
                free(t2->args);
                free(t2);
            }
        }
    }

    // wait for threads before exiting
    struct thread_list_entry *t;
    SLIST_FOREACH(t, &thread_list_head, entries) {
        pthread_join(t->thread, NULL);
    }
    
    close(socket_fd);

    #if USE_AESD_CHAR_DEVICE != 1
        unlink(TMP_PATH);
    #endif
    return EXIT_SUCCESS;
}
