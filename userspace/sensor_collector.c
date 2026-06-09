#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include "sensor_ioctl.h"

#define FILE_SIZE_MAX (1024*1024)

char socket_path[256] = "/tmp/run.socket";
char device_path[256] = "/dev/sensor0";
char log_path[256] = "/tmp/sensor_collector.log";

/* Struct W_zone */
struct w_zone {
    struct sensor_sample w_buf[OUT_BUFFER_MAX];
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int write_count;
    int record_count[SENSOR_COUNT];
    int read_count[SENSOR_COUNT];
    int error_count[SENSOR_COUNT];
};

/* Define W_ring */
static struct w_zone W_ring = {
    .head = 0,
    .tail = 0,
    .count = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .write_count = 0,
    .record_count = {0},
    .read_count = {0},
    .error_count = {0},
};
/* Curtime per sensor/reader */
static time_t s_start_t[SENSOR_COUNT] = {0,0,0};
/* Signal flag */
static volatile sig_atomic_t flag_keep_running = 1;

static volatile sig_atomic_t intervals[SENSOR_COUNT] = {1000, 1000, 1000};

void handle_signals(int signo) {
    if (signo == SIGTERM || signo == SIGINT) {
        flag_keep_running = 0;
    }
    else if (signo == SIGUSR1) {
        for (int i = 0; i < SENSOR_COUNT; i++) {
            intervals[i] *= 2;
        }
    }
    else if (signo == SIGUSR2) {
        for (int i = 0; i < SENSOR_COUNT; i++) {
            intervals[i] /= 2;
            if (intervals[i] < 100) {
                intervals[i] = 100;
            }
        }
    }
}

/* File size check */
static int f_size_check (FILE *fi) {
    struct stat st;

    if (fstat(fileno(fi), &st) == 0) {
        return st.st_size >= FILE_SIZE_MAX;
    }
    else {
        write(2,"Cant read file stat\n", 21);
        return -1;
    }
    
}

static int f_rot (int rot, FILE** f_pp) {
    if (rot == 1) {
        
        fclose(*f_pp);
        char hold[300];
        snprintf(hold,sizeof(hold),"%s.1",log_path);
        
        remove (hold);
        if (rename(log_path,hold) != 0) {
           write(2, "Rename failed\n", 15);
           return 1;
        }
        *f_pp = fopen(log_path,"a");
        
        if (!*f_pp) {
            write(2,"Cant create log\n",17);
            return 1;
        }
        return 0;
    }
    else if (rot == -1) return 1;
    else return 0;
}
/* Quick access to a fd */
int quick_fd (int * fd , int id) {
    int res;
    *fd = open(device_path, O_RDONLY);
    if (*fd < 0) return 1;
    res = ioctl(*fd, SENSOR_SELECT, id);
    if (res == 0) return 0;
    else return 1;
}

/* Lock and Cond for pause/resume */
static pthread_mutex_t pr_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pr_cond[SENSOR_COUNT] = {PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER};
static volatile sig_atomic_t rt_paused[SENSOR_COUNT] = {0,0,0};

/* Function to convert argv from ctl to int */
static int ctoi (char * arg) {
    if (strcmp(arg, "0") == 0) {
        return 0;
    }
    else if (strcmp(arg, "1") == 0) {
        return 1;
    }
    else if (strcmp(arg, "2") == 0) {
        return 2;
    }
    else return -1;
}
/* Reader thread */
void * s_reader_thread (void * arg) {
    int id = *(int*)arg;
    free(arg);
    struct sensor_sample sample;
    int s_len;
    int res;
    int fd = -1;
    int itv;
    s_start_t[id] = time(NULL);
    while (flag_keep_running) {
    
    pthread_mutex_lock(&pr_lock);
    while (flag_keep_running && rt_paused[id]){
        pthread_cond_wait(&pr_cond[id],&pr_lock);
    }
    pthread_mutex_unlock(&pr_lock);
    
    if (fd < 0) {
    fd = open(device_path, O_RDONLY);
        if (fd < 0) {
    
        write(2,"Error opening fd to device\n",28);
        sleep(1);
        continue;
        }
    }
    
    res = ioctl(fd, SENSOR_SELECT, id);
    if (res != 0) {
        write(2,"Select error\n",14);
        close(fd);
        fd = -1;
        sleep(1);
        continue;
    }
    
    
    while((fd >= 0) && flag_keep_running) {
        s_len=ioctl(fd, SENSOR_READ_SAMPLE, &sample);
        if (!s_len) {
        pthread_mutex_lock(&W_ring.lock);
        if (W_ring.count == OUT_BUFFER_MAX) {
            W_ring.tail = (W_ring.tail + 1) % OUT_BUFFER_MAX;
            W_ring.error_count[id] ++;
        }
        else W_ring.count ++;
        
        W_ring.w_buf[W_ring.head] = sample;
        W_ring.head = (W_ring.head + 1) % OUT_BUFFER_MAX;
        W_ring.read_count[id] ++;
        pthread_cond_signal(&W_ring.cond);
        pthread_mutex_unlock(&W_ring.lock);
        itv = intervals[id]*1000;
        usleep(itv);
        
        }
        
        else {
            if (errno == EAGAIN) {
                write(1,"Sensor buffer is empty\n",24);
                sleep(2);
            }
            else if  (errno == EFAULT) {
                write(2,"Memory copy issue\n", 19);
                syslog(LOG_ERR, "[Sensor_reader] Memory copy issue");
                sleep(5);
            }
            else {
                write(2,"Driver issue\n", 14);
                syslog(LOG_ERR, "[Sensor_reader] Driver issue");
                close(fd);
                fd = -1;
                sleep(1);
            }
        }
    }
    }
    if (fd >= 0) {
        write(1,"Closed thread on command\n",26);
        close(fd);
    }
    return NULL;
};

/* Kernel boot Unix epoch */

int64_t k_boot_t() {
    struct timespec real_time, boot_time;

    clock_gettime(CLOCK_REALTIME, &real_time);
    clock_gettime(CLOCK_MONOTONIC, &boot_time);

    int64_t epoch_boot_sec = real_time.tv_sec - boot_time.tv_sec;
    int64_t epoch_boot_nsec = real_time.tv_nsec - boot_time.tv_nsec;
    
    if (epoch_boot_nsec < 0) {
        epoch_boot_sec -= 1;
        epoch_boot_nsec += 1000000000;
    }

    int64_t boot_time_us = (epoch_boot_sec * 1000000) + (epoch_boot_nsec / 1000);
    return boot_time_us;
};

static int64_t boot_t;

/* Convert time format */
void time_conv (struct sensor_sample sample, char * time_ptr, long * micro) {
    
    int64_t cur_time = sample.timestamp_us + boot_t;
    time_t sec = cur_time / 1000000;
    long ms = (cur_time % 1000000) / 1000;
    struct tm tm;
    localtime_r(&sec, &tm);
    strftime(time_ptr, 64,"%Y-%m-%d %H:%M:%S",&tm);
    *micro = ms;
};


/* Writer thread */
void * b_writer_thread (void * arg) {
    
    struct sensor_sample sample;
    char t_ptr[64];
    long  micro;
    FILE* f_log = fopen(log_path, "a");
    if (!f_log) {
        write(2,"Cant create log\n",17);
        return NULL;
    }
    
    while (1){
    pthread_mutex_lock(&W_ring.lock);
    
    while ((W_ring.count == 0) && flag_keep_running) {
        pthread_cond_wait(&W_ring.cond,&W_ring.lock);
    }
    
    if ((W_ring.count == 0) && !flag_keep_running) {
    pthread_mutex_unlock(&W_ring.lock);
    break;
    }
    
    sample = W_ring.w_buf[W_ring.tail];
    W_ring.tail = (W_ring.tail + 1) % OUT_BUFFER_MAX;
    W_ring.count --;
    W_ring.write_count ++;
    W_ring.record_count[sample.sensor_id] ++;
    pthread_mutex_unlock(&W_ring.lock);
    time_conv(sample, t_ptr, &micro);
    switch (sample.sensor_id) {
    case SENSOR_TYPE_TEMPERATURE :
    fprintf(f_log, "[%s.%03ld] SENSOR=0 TYPE=TEMP VALUE=%.2f UNIT=°C\n", t_ptr,micro, sample.value / 100.0);
    break;
    case SENSOR_TYPE_HUMIDITY :
    fprintf(f_log, "[%s.%03ld] SENSOR=1 TYPE=HUMID VALUE=%.2f UNIT=%%RH\n", t_ptr,micro, sample.value / 100.0);
    break;
    case SENSOR_TYPE_PRESSURE :
    fprintf(f_log, "[%s.%03ld] SENSOR=2 TYPE=PRESS VALUE=%.2f UNIT=hPa\n", t_ptr,micro, sample.value / 100.0);
    break;
    default:
    break;
    }
    fflush(f_log);
    if(f_rot(f_size_check(f_log),&f_log)) {
        return NULL;
    }
    }
    fclose(f_log);
    return NULL;
};

/* IPC listener thread */
static void* s_ipc_thread(void *arg) {
    int server_fd, client_fd;
    struct sockaddr_un addr;
    char buffer[128];
    
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        write(2,"Socket create failed\n",22);
        return NULL;
    }

    unlink(socket_path);

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) < 0) {
        write(2,"Binding failed\n",16);
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 5) < 0) {
        write(2,"Listen failed\n",15);
        close(server_fd);
        return NULL;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000; 
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    write(1,"Setup socket for 0.5s check\n",29);

    while (flag_keep_running) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            continue; 
        }

        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_received > 0) {
            buffer[strcspn(buffer, "\r\n")] = 0;
            
            char cmd[10] = {0};
            char tar[10] = {0};
            int val = 0;
            
            int sel_id;
            
            int fd,res,ret;
            char send_buf[256];
            
            int parsed_b = sscanf(buffer, "%9s %9s %d",cmd,tar,&val);
            if (parsed_b < 1) {
                send(client_fd, "Not sufficient arg\n", 19, 0);
                close(client_fd);
                continue;
            }
            /* Cmd is stats */
            if (strcmp(cmd, "stats") == 0) {
                if (parsed_b < 2) {
                    send(client_fd, "Incorrect syntax for stats\n", 29, 0);
                    close(client_fd);
                    continue;
                }
                sel_id = ctoi(tar);
                if ((sel_id >= 0) && (sel_id < 3)) {
                    struct sensor_stats h_stats;
                    ret=quick_fd(&fd, sel_id);
                    if (ret!=0) {
                        send(client_fd, "Cant open/select sensor\n", 24, 0);
                        close(client_fd);
                        continue;
                    }
                    res = ioctl(fd, SENSOR_GET_STATS, &h_stats);
                    close(fd);
                    
                    if (res!=0) {
                        send(client_fd, "Cant get sensor stats\n", 22, 0);
                        close(client_fd);
                        continue;
                    }
                    snprintf(send_buf,sizeof(send_buf), "{\n sensor_id : %d\n read_count : %d\n error_count : %d\n sampling_rate : %d\n last_value : %d\n}\n",  h_stats.sensor_id, h_stats.read_count, h_stats.error_count,                    h_stats.sampling_rate, h_stats.last_value);
                    send(client_fd, send_buf, strlen(send_buf),0);
                }
                
                else if (strcmp(tar, "all") == 0) {
                    send(client_fd, "SENSOR\tTYPE\tRATE(Hz)\tREAD_CNT\tERR_CNT\tLAST_VALUE\n",49,0);
                    send(client_fd, "------------------------------------------------------------------\n", 67,0);
                    char * type[3] = {"TEMP","HUMID","PRESS"};
                    char * sym[3] = {"°C","%RH","hPa"};
                    for (int i=0 ; i<SENSOR_COUNT ; i++) {
                    struct sensor_stats h_stats;
                    ret = quick_fd(&fd, i);
                    if (ret!=0) {
                        send(client_fd, "Cant open/select sensor\n", 24, 0);
                        close(client_fd);
                        continue;
                        }
                    res= ioctl(fd, SENSOR_GET_STATS, &h_stats);
                    close(fd);
                    
                    if (res!=0) {
                        send(client_fd, "Cant get sensor stats\n", 22, 0);
                        close(client_fd);
                        continue;
                    }
                    snprintf(send_buf,sizeof(send_buf), "%d\t%s\t%d\t\t%d\t\t%d\t%.2f%s\n",  h_stats.sensor_id, type[i],h_stats.sampling_rate, h_stats.read_count, h_stats.error_count, h_stats.last_value/100.0,sym[i]);
                    send(client_fd, send_buf, strlen(send_buf),0);
                }
                }
                
            }
            
            /* Cmd is set-rate */ 
            else if (strcmp(cmd, "set-rate") == 0) {
                if (parsed_b < 3) {
                    send(client_fd, "Incorrect syntax for set-rate\n", 30, 0);
                    close(client_fd);
                    continue;
                }
                if ( val < 1) val = 1;
                else if (val > 100) val =100;
                sel_id = ctoi(tar);
                if ((sel_id >= 0) && (sel_id < 3)) {
                    
                    intervals[sel_id] = 1000/val; 
                    send(client_fd, "OK\n", 3, 0);
                }
                else if (strcmp(tar, "all") == 0) {
                    
                    int s_r_all = 1000/val;
                    intervals[0] = s_r_all;
                    intervals[1] = s_r_all;
                    intervals[2] = s_r_all;
                    send(client_fd, "OK\n", 3, 0);
                }
            }
            
            /* Cmd is status*/
            else if (strcmp(cmd, "status") == 0) {
                
                int record_copy[3];
                long uptime[SENSOR_COUNT] = {0,0,0};
                time_t current_time = time(NULL);
                
                pthread_mutex_lock(&W_ring.lock);
                memcpy(record_copy,W_ring.record_count, sizeof(W_ring.record_count));
                pthread_mutex_unlock(&W_ring.lock);
                
                for (int i=0 ; i<SENSOR_COUNT ; i++) {
                    if (s_start_t[i] > 0) {
                    uptime[i] = (long)difftime(current_time,s_start_t[i]);
                    }
                    else uptime[i] = 0;
                }
                snprintf(send_buf,sizeof(send_buf), "{\n{\n Sensor 0 : Temperature\n Record count : %d\n Uptime : %-12ld\n}\n{\n Sensor 1 : Humidity\n Record count : %d\n Uptime : %-12ld\n}\n{\n Sensor 2 : Pressure\n Record count : %d\n Uptime : %-12ld\n}\n}\n",record_copy[0], uptime[0],record_copy[1], uptime[1],record_copy[2],uptime[2]);
                send(client_fd, send_buf, strlen(send_buf),0);
            }
            
            /* Cmd is pause/resume */
            else if (strcmp(cmd, "pause") == 0) {
                if (parsed_b < 2) {
                    send(client_fd, "Incorrect syntax for pause\n", 27, 0);
                    close(client_fd);
                    continue;
                }
                sel_id = ctoi(tar);
                if ((sel_id >= 0) && (sel_id < 3)) {
                rt_paused[sel_id] = 1;
                send(client_fd, "OK\n", 3, 0);
                }
            }
            else if (strcmp(cmd, "resume") == 0) {
                if (parsed_b < 2) {
                    send(client_fd, "Incorrect syntax for resume\n", 28, 0);
                    close(client_fd);
                    continue;
                }
                sel_id = ctoi(tar);
                if ((sel_id >= 0) && (sel_id < 3)) {
                rt_paused[sel_id] = 0;
                
                pthread_mutex_lock(&pr_lock);
                pthread_cond_signal(&pr_cond[sel_id]);
                pthread_mutex_unlock(&pr_lock);
                
                send(client_fd, "OK\n", 3, 0);
                }
            }
            
            /* Cmd is reset */
            else if (strcmp(cmd, "reset") == 0) {
                if (parsed_b < 2) {
                    send(client_fd, "Incorrect syntax for reset\n", 27, 0);
                    close(client_fd);
                    continue;
                }
                sel_id = ctoi(tar);
                if ((sel_id >= 0) && (sel_id < 3)) {
                ret=quick_fd(&fd, sel_id);
                if (ret!=0) {
                    send(client_fd, "Cant open/select sensor\n", 24, 0);
                    close(client_fd);
                    continue;
                }
                res= ioctl(fd, SENSOR_RESET);
                close(fd);
                    
                if (res!=0) {
                    send(client_fd, "Reset sensor failed\n", 20, 0);
                    close(client_fd);
                    continue;
                }
                snprintf(send_buf,sizeof(send_buf),"Reset sensor %d successful\n", sel_id);
                send(client_fd, send_buf, strlen(send_buf),0);
                }
                
                else if (strcmp(tar, "all") == 0) {
                for (int i=0 ; i<SENSOR_COUNT ; i++) {
                    ret = quick_fd(&fd, i);
                    if (ret!=0) {
                    send(client_fd, "Cant open/select sensor\n", 24, 0);
                    close(client_fd);
                    continue;
                    }
                    res= ioctl(fd, SENSOR_RESET);
                    close(fd);
                    
                    if (res!=0) {
                        send(client_fd, "Reset sensor failed\n", 20, 0);
                        close(client_fd);
                        continue;
                    }
                    snprintf(send_buf,sizeof(send_buf),"Reset sensor %d successful\n", i);
                    send(client_fd, send_buf, strlen(send_buf),0);
                }
                }
                
            }
            
            /* Cmd is set-s-rate */
            else if (strcmp(cmd, "set-srate") == 0) {
                if (parsed_b < 3) {
                    send(client_fd, "Incorrect syntax for set-srate\n", 32, 0);
                    close(client_fd);
                    continue;
                }
                if (val > 10) val = 10;
                else if (val < 1) val =1;
                sel_id = ctoi(tar);
                if ((sel_id >= 0) && (sel_id < 3)) {
                ret=quick_fd(&fd, sel_id);
                if (ret!=0) {
                    send(client_fd, "Cant open/select sensor\n", 24, 0);
                    close(client_fd);
                    continue;
                }
                res= ioctl(fd, SENSOR_SET_RATE,val);
                close(fd);
                    
                if (res!=0) {
                    send(client_fd, "Set sampling rate sensor failed\n", 32, 0);
                    close(client_fd);
                    continue;
                }
                snprintf(send_buf,sizeof(send_buf),"Set sampling rate sensor %d successful\n", sel_id);
                send(client_fd, send_buf, strlen(send_buf),0);
                }
                
                else if (strcmp(tar, "all") == 0) {
                for (int i=0 ; i<SENSOR_COUNT ; i++) {
                ret = quick_fd(&fd, i);
                if (ret!=0) {
                    send(client_fd, "Cant open/select sensor\n", 24, 0);
                    close(client_fd);
                    continue;
                }
                res= ioctl(fd, SENSOR_SET_RATE,val);
                close(fd);
                    
                if (res!=0) {
                    send(client_fd, "Set sampling rate sensor failed\n", 32, 0);
                    close(client_fd);
                    continue;
                }
                snprintf(send_buf,sizeof(send_buf),"Set sampling rate sensor %d successful\n", i);
                send(client_fd, send_buf, strlen(send_buf),0);
                }
                }
                
            }
            
            /* Not syntax correct */
            else {
            send(client_fd, "Incorrect syntax\n", 17, 0);
            close(client_fd);
            continue;
            }
        }
        close(client_fd);
    }

    close(server_fd);
    unlink(socket_path);
    write(1,"IPC thread closed\n",19);
    return NULL;
}

//------------MAIN FUNCTION----------------//

int main(int argc, char* argv[]) {
    
    /* Runtime setup */
    int opt;

    while ((opt = getopt(argc, argv, "d:l:s:i")) != -1) {

    switch (opt) {

    case 'd':
        strncpy(device_path,
                optarg,
                sizeof(device_path)-1);
        break;

    case 'l':
        strncpy(log_path,
                optarg,
                sizeof(log_path)-1);
        break;

    case 's':
        strncpy(socket_path,
                optarg,
                sizeof(socket_path)-1);
        break;

    case 'i':
        int ms = atoi(optarg);

        if (ms < 100)
            ms = 100;

        for (int i=0;i<SENSOR_COUNT;i++)
            intervals[i] = ms;

        break;

    default:
        return 1;
    }
    }
    
    boot_t = k_boot_t();
    pthread_t r_thread[SENSOR_COUNT];
    pthread_t w_thread;
    pthread_t ipc_thread;
    
    write(1, "Starting thread\n", 17); 
    
    struct sigaction sa = {0};
    sa.sa_handler = handle_signals;
    sa.sa_flags = SA_RESTART;

    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    
    /* Writer thread first */
    if (pthread_create(&w_thread, NULL, b_writer_thread, NULL) != 0) {
        write(2, "Cant create writer thread\n", 27);
        return 1;
    }
    
    /* Reader thread loop */
    int created_readers = 0;
    
    for (int i = 0; i < SENSOR_COUNT; i++) {
        int *id = malloc(sizeof(int));
        if (!id) {
            flag_keep_running = 0;
            break;
        }
        *id = i;
        if (pthread_create(&r_thread[i], NULL, s_reader_thread, id) != 0) {
            write(2, "Cant create reader thread\n", 27);
            free(id);
            flag_keep_running = 0;
            break;
        }
        created_readers++;
    }
    
    /* Ipc thread */
    int ipc_created = 0;
    if (flag_keep_running == 1) {
        if (pthread_create(&ipc_thread, NULL, s_ipc_thread, NULL) != 0) {
            write(2, "Cant create ipc thread\n", 24);
            flag_keep_running = 0; 
        } else {
            ipc_created = 1;
        }
    }
    
    if (ipc_created) {
        pthread_join(ipc_thread, NULL);
    }
    /* Incase there are still some paused r_thread after ipc_thread ended */
    for (int i =0; i < SENSOR_COUNT; i++) {
        pthread_mutex_lock(&pr_lock);
        pthread_cond_broadcast(&pr_cond[i]);
        pthread_mutex_unlock(&pr_lock);
    }
    /* Incase of error in r_thread creating */
    for (int i = 0; i < created_readers; i++) {
        pthread_join(r_thread[i], NULL);
    }
    

    pthread_mutex_lock(&W_ring.lock);
    pthread_cond_broadcast(&W_ring.cond);
    pthread_mutex_unlock(&W_ring.lock);
    
    pthread_join(w_thread, NULL);
    
    write(1, "Closed all\n", 12);
    
    return 0;    
}

