#include <cstdio>
#include <map>
#include <string>
#include <ctime>
using namespace std;

#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>

#define LET(it, val) typeof(val) it(val)
#define EACH(it, cont)  for(LET(it, cont.begin());   it != cont.end();  ++it)

const char *csv_file = "/proc/csv_perfiso_stats";
FILE *fp;
typedef unsigned long long u64;

struct stats {
    u64 tx;
    u64 rx;
    stats():tx(0), rx(0) {}
};

map<string,stats> db[2];

int interval_us = 100*1000;

void die(const char *msg) {
    fprintf(stderr, msg);
    exit(1);
}

void read_data(int i) {
    char line[256];
    char dir[32], klass[32];
    u64 bytes;
    int n, j;
    rewind(fp);

    while(!feof(fp)) {
        fscanf(fp, "%s\n", line);
        for(j = 0; line[j] != '\0'; j++) {
            if(line[j] == ',') line[j] = ' ';
        }

        n = sscanf(line, "%s %s %llu", dir, klass, &bytes);
        if(n != 3) {
            break;
        }

        if(dir[0] == 't') {
            /* TX */
            db[i][klass].tx = bytes;
        } else {
            db[i][klass].rx = bytes;
        }
    }
}


/* bits/us = Mbps */
inline double rate(u64 bytes, int us) {
    return bytes * 8.0 / us;
}

inline double gtod() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((double)tv.tv_sec + tv.tv_usec / 1e6);
}

void print_data(int curr) {
    int prev = (curr + 1) & 1;

    EACH(it, db[prev]) {
        string key = it->first;
        stats value = it->second;
        stats value2 = db[curr][key];
        u64 tx_bytes = value2.tx - value.tx;
        u64 rx_bytes = value2.rx - value.rx;

        double tx_rate = rate(tx_bytes, interval_us);
        double rx_rate = rate(rx_bytes, interval_us);

        printf("%.3f,%s,%.3lf,%.3lf\n", gtod(), key.c_str(), tx_rate, rx_rate);
    }

    fflush(stdout);
}

void monitor_loop() {
    /* Populate first sample */
    int i = 0;
    read_data(i++);
    usleep(interval_us);

    while(1) {
        rewind(fp);
        read_data(i);
        print_data(i);
        i = (i+1)&1;
        usleep(interval_us);
    }
}

int main(int argc, char *argv[]) {
    if(argc > 1) {
        interval_us = max(1000, atoi(argv[1]));
    }

    fp = fopen(csv_file, "r");
    if(fp == NULL)
        die("CSV file not found\n");
    printf("#time,class,txrate,rxrate\n");
    monitor_loop();
    return 0;
}
