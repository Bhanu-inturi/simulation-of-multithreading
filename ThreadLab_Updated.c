#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
/*
 * ThreadLab - Cross-platform Multithreading & Scheduling Simulator
 * Requirements: C11 compiler and Raylib 5.x
 *
 * macOS/Linux: pthreads + POSIX semaphores
 * Windows:     Win32 threads, critical sections, and semaphores
 *
 * The scheduling and deadlock panels are deterministic teaching simulations.
 * The race-condition panel uses native threads. Its "unprotected" mode
 * separates each read and write with a mutex so it demonstrates lost updates
 * without invoking an undefined C data race.
 */
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HANDLE ThreadHandle;
typedef HANDLE Semaphore;
typedef CRITICAL_SECTION Mutex;
typedef DWORD (WINAPI *ThreadFn)(LPVOID);
#define THREAD_RETURN DWORD WINAPI
#define THREAD_ARG LPVOID
#define THREAD_DONE 0
static void mutex_init(Mutex *m) { InitializeCriticalSection(m); }
static void mutex_destroy(Mutex *m) { DeleteCriticalSection(m); }
static void mutex_lock(Mutex *m) { EnterCriticalSection(m); }
static void mutex_unlock(Mutex *m) { LeaveCriticalSection(m); }
static void sleep_ms(unsigned ms) { Sleep(ms); }
static int semaphore_init(Semaphore *s, unsigned initial, unsigned maximum) {
    *s = CreateSemaphore(NULL, (LONG)initial, (LONG)maximum, NULL);
    return *s != NULL;
}
static void semaphore_destroy(Semaphore *s) { if (*s) CloseHandle(*s); }
static void semaphore_wait(Semaphore *s) { WaitForSingleObject(*s, INFINITE); }
static void semaphore_post(Semaphore *s) { ReleaseSemaphore(*s, 1, NULL); }
static double monotonic_seconds(void) {
    LARGE_INTEGER now, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)frequency.QuadPart;
}
static int thread_create(ThreadHandle *h, ThreadFn fn, void *arg) {
    *h = CreateThread(NULL, 0, fn, arg, 0, NULL); return *h != NULL;
}
static void thread_join(ThreadHandle h) {
    if (h) { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
}
#else
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#ifdef __APPLE__
#include <fcntl.h>
#endif
typedef pthread_t ThreadHandle;
typedef pthread_mutex_t Mutex;
#ifdef __APPLE__
typedef struct { sem_t *handle; char name[64]; } Semaphore;
#else
typedef sem_t Semaphore;
#endif
#define THREAD_RETURN void *
#define THREAD_ARG void *
#define THREAD_DONE NULL
static void mutex_init(Mutex *m) { pthread_mutex_init(m, NULL); }
static void mutex_destroy(Mutex *m) { pthread_mutex_destroy(m); }
static void mutex_lock(Mutex *m) { pthread_mutex_lock(m); }
static void mutex_unlock(Mutex *m) { pthread_mutex_unlock(m); }
#ifdef __APPLE__
static int semaphore_init(Semaphore *s, unsigned initial, unsigned maximum) {
    static unsigned sequence=0;
    (void)maximum;
    snprintf(s->name,sizeof(s->name),"/threadlab_%ld_%u",(long)getpid(),sequence++);
    sem_unlink(s->name);
    s->handle=sem_open(s->name,O_CREAT|O_EXCL,0600,initial);
    return s->handle != SEM_FAILED;
}
static void semaphore_destroy(Semaphore *s) {
    if(s->handle && s->handle!=SEM_FAILED) sem_close(s->handle);
    sem_unlink(s->name);
}
static void semaphore_wait(Semaphore *s) {
    while(sem_wait(s->handle)!=0 && errno==EINTR) { }
}
static void semaphore_post(Semaphore *s) { sem_post(s->handle); }
#else
static int semaphore_init(Semaphore *s, unsigned initial, unsigned maximum) {
    (void)maximum;
    return sem_init(s, 0, initial) == 0;
}
static void semaphore_destroy(Semaphore *s) { sem_destroy(s); }
static void semaphore_wait(Semaphore *s) {
    while(sem_wait(s) != 0 && errno==EINTR) { }
}
static void semaphore_post(Semaphore *s) { sem_post(s); }
#endif
static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}
static void sleep_ms(unsigned ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
static int thread_create(ThreadHandle *h, void *(*fn)(void *), void *arg) {
    return pthread_create(h, NULL, fn, arg) == 0;
}
static void thread_join(ThreadHandle h) { pthread_join(h, NULL); }
#endif

#define NTHREADS 5
#define TIMELINE_CAP 180
#define W 1120
#define H 760
#define BUFFER_SIZE 5
#define PC_ITEMS 8
#define PC_LOG_CAP 16

typedef enum {
    TAB_HOME, TAB_RR, TAB_PRIORITY, TAB_RACE, TAB_DEADLOCK, TAB_TIMELINE,
    TAB_PRODUCER_CONSUMER, TAB_THREAD_TIME
} Tab;
typedef struct { int burst, remaining, priority, completed; } Job;

static const Color BG={13,18,32,255}, PANEL={23,31,51,255},
    PANEL2={30,41,65,255}, TXT={235,241,250,255}, MUTED={143,158,183,255},
    CYAN_TONE={58,204,220,255}, GREEN_TONE={76,214,153,255}, ORANGE_TONE={255,176,82,255},
    RED_TONE={246,103,117,255}, PURPLE_TONE={164,133,255,255};
static const Color tc[NTHREADS]={{58,204,220,255},{164,133,255,255},
    {76,214,153,255},{255,176,82,255},{244,125,166,255}};
static const char *tn[NTHREADS]={"THREAD 1","THREAD 2","THREAD 3","THREAD 4","THREAD 5"};

static Tab tab=TAB_HOME;
static Job jobs[NTHREADS];
static int quantum=2, rr_index=0, sim_running=0, sim_done=0;
static int tick_accum=0, clock_tick=0, timeline[TIMELINE_CAP], timeline_count=0;
static int deadlock_active=0, deadlock_prevented=0;

static Mutex race_mutex, race_state_mutex;
static ThreadHandle race_handles[2];
static int race_handles_valid[2]={0,0};
static volatile int race_finished=0;
static int race_started=0, race_safe=0, race_counter=0;
static int race_expected=1000;
static int race_logical_mode=0;

/* Producer-consumer shared bounded buffer and semaphore state. */
static Semaphore pc_empty, pc_full;
static Mutex pc_mutex;
static int pc_buffer[BUFFER_SIZE], pc_head=0, pc_tail=0, pc_count=0;
static int pc_log[PC_LOG_CAP], pc_log_count=0;
static int pc_started=0, pc_finished=0, pc_produced=0, pc_consumed=0;
static ThreadHandle pc_handles[2];
static int pc_handles_valid[2]={0,0};

/* Per-thread wall-clock timing demonstration. */
typedef struct { int id; uint64_t iterations; double elapsed; uint64_t checksum; int done; } TimingJob;
static TimingJob timing_jobs[NTHREADS];
static ThreadHandle timing_handles[NTHREADS];
static int timing_handles_valid[NTHREADS]={0,0,0,0,0};
static int timing_started=0, timing_finished=0;
static Mutex timing_mutex;

#ifdef _WIN32
static THREAD_RETURN race_worker(THREAD_ARG arg);
#else
static THREAD_RETURN race_worker(THREAD_ARG arg);
#endif


static THREAD_RETURN producer_worker(THREAD_ARG arg);
static THREAD_RETURN consumer_worker(THREAD_ARG arg);
static THREAD_RETURN timing_worker(THREAD_ARG arg);

static void pc_log_event(int value, int produced) {
    if (pc_log_count < PC_LOG_CAP) {
        pc_log[pc_log_count++] = produced ? value : -value;
    } else {
        memmove(pc_log, pc_log + 1, (PC_LOG_CAP - 1) * sizeof(int));
        pc_log[PC_LOG_CAP - 1] = produced ? value : -value;
    }
}

static void reset_jobs(void) {
    const int bursts[NTHREADS]={7,4,9,5,6};
    const int priorities[NTHREADS]={3,5,1,4,2};
    for (int i=0;i<NTHREADS;i++) {
        jobs[i].burst=bursts[i]; jobs[i].remaining=bursts[i];
        jobs[i].priority=priorities[i]; jobs[i].completed=0;
    }
    rr_index=0; sim_running=0; sim_done=0; clock_tick=0;
    tick_accum=0; timeline_count=0;
}
static int all_done(void) {
    for(int i=0;i<NTHREADS;i++) if(jobs[i].remaining>0) return 0;
    return 1;
}
static int next_rr(void) {
    for(int k=0;k<NTHREADS;k++) {
        int i=(rr_index+k)%NTHREADS;
        if(jobs[i].remaining>0) { rr_index=(i+1)%NTHREADS; return i; }
    }
    return -1;
}
static int next_priority(void) {
    int best=-1;
    for(int i=0;i<NTHREADS;i++) {
        if(jobs[i].remaining>0 &&
           (best<0 || jobs[i].priority>jobs[best].priority)) best=i;
    }
    return best;
}
static void scheduler_step(void) {
    if(!sim_running || sim_done) return;
    int id=(tab==TAB_PRIORITY)?next_priority():next_rr();
    if(id<0) { sim_running=0; sim_done=1; return; }
    int slice=(tab==TAB_RR && jobs[id].remaining>quantum)?quantum:1;
    if(tab==TAB_RR) slice=jobs[id].remaining<quantum?jobs[id].remaining:quantum;
    /* Priority mode is non-preemptive: run the selected job to completion. */
    if(tab==TAB_PRIORITY) slice=jobs[id].remaining;
    for(int s=0;s<slice && timeline_count<TIMELINE_CAP;s++) {
        timeline[timeline_count++]=id; clock_tick++;
    }
    jobs[id].remaining-=slice;
    if(jobs[id].remaining<=0) { jobs[id].remaining=0; jobs[id].completed=1; }
    if(all_done()) { sim_running=0; sim_done=1; }
}
static void start_scheduler(void) {
    reset_jobs(); sim_running=1;
}
static int ui_button(Rectangle r,const char *label,Color c,int enabled) {
    Vector2 m=GetMousePosition();
    int hover=CheckCollisionPointRec(m,r);
    Color fill=enabled?(hover?ColorBrightness(c,0.16f):c):(Color){63,73,92,255};
    DrawRectangleRounded(r,0.2f,8,fill);
    int fs=16, tw=MeasureText(label,fs);
    DrawText(label,(int)(r.x+(r.width-tw)/2),(int)(r.y+(r.height-fs)/2),
             fs,enabled?BG:MUTED);
    return enabled && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
static void panel(Rectangle r) { DrawRectangleRounded(r,0.12f,8,PANEL); }
static void header(void) {
    DrawText("THREADLAB",34,20,14,CYAN_TONE);
    DrawText("Multithreading Simulator",34,43,29,TXT);
    DrawText("OPERATING SYSTEMS  /  CONCURRENCY & SYNCHRONIZATION",36,82,11,MUTED);
    DrawCircle(1030,38,5,GREEN_TONE); DrawText("SYSTEM ONLINE",1043,31,11,GREEN_TONE);
    DrawLine(34,105,1086,105,(Color){54,67,91,255});
}
static void tabs(void) {
    const char *names[]={"HOME","ROUND ROBIN","PRIORITY","RACE","DEADLOCK",
                         "TIMELINE","PRODUCER-CONSUMER","THREAD TIME"};
    const int widths[]={76,116,92,75,92,88,190,112};
    int x=34;
    for(int i=0;i<8;i++) {
        Rectangle r={(float)x,119,(float)widths[i],35};
        int active=(int)tab==i;
        DrawRectangleRounded(r,0.18f,6,active?CYAN_TONE:PANEL);
        int tw=MeasureText(names[i],10);
        DrawText(names[i],x+(widths[i]-tw)/2,131,10,active?BG:TXT);
        if(CheckCollisionPointRec(GetMousePosition(),r) &&
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            tab=(Tab)i;
            if(tab==TAB_RR || tab==TAB_PRIORITY) reset_jobs();
        }
        x+=widths[i]+5;
    }
}
static void title(const char *t,const char *sub) {
    DrawText(t,36,174,23,TXT); DrawText(sub,38,205,12,MUTED);
}
static void draw_job_rows(int show_priority) {
    for(int i=0;i<NTHREADS;i++) {
        float y=260+i*67;
        panel((Rectangle){36,y,650,56});
        DrawCircle(57,y+28,6,tc[i]); DrawText(tn[i],75,(int)y+9,13,TXT);
        DrawText(TextFormat("BURST %d",jobs[i].burst),75,(int)y+31,10,MUTED);
        Rectangle b={220,y+22,270,12}; DrawRectangleRounded(b,0.5f,6,PANEL2);
        float done=(float)(jobs[i].burst-jobs[i].remaining)/jobs[i].burst;
        if(done>0) DrawRectangleRounded((Rectangle){b.x,b.y,b.width*done,b.height},
                                        0.5f,6,tc[i]);
        DrawText(TextFormat("%d / %d",jobs[i].burst-jobs[i].remaining,jobs[i].burst),
                 505,(int)y+20,12,TXT);
        if(show_priority) DrawText(TextFormat("PRIORITY %d",jobs[i].priority),
                                    570,(int)y+21,11,ORANGE_TONE);
        else DrawText(jobs[i].completed?"DONE":"READY",570,(int)y+21,11,
                      jobs[i].completed?GREEN_TONE:MUTED);
    }
}
static void draw_scheduler(int priority) {
    title(priority?"Priority Scheduling":"Round Robin Scheduling",
          priority?"Non-preemptive priority: the highest-priority ready thread runs first."
                   :"Each ready thread receives a configurable time quantum in rotation.");
    panel((Rectangle){710,250,375,360});
    DrawText("SCHEDULER CONTROLS",733,271,13,TXT);
    DrawText(priority?"Higher number = higher priority":"TIME QUANTUM",733,305,11,MUTED);
    if(!priority) {
        DrawText(TextFormat("%d",quantum),737,337,30,CYAN_TONE);
        if(ui_button((Rectangle){790,332,42,36},"-",PURPLE_TONE,quantum>1)) quantum--;
        if(ui_button((Rectangle){840,332,42,36},"+",PURPLE_TONE,quantum<6)) quantum++;
    } else {
        DrawText("Priority order:",733,342,12,MUTED);
        DrawText("T2 > T4 > T1 > T5 > T3",733,365,15,ORANGE_TONE);
    }
    if(ui_button((Rectangle){733,410,145,42},"START",GREEN_TONE,!sim_running)) start_scheduler();
    if(ui_button((Rectangle){890,410,155,42},"STEP",CYAN_TONE,!sim_done)) {
        if(!sim_running && !sim_done) sim_running=1;
        scheduler_step(); sim_running=0;
    }
    if(ui_button((Rectangle){733,463,145,42},sim_running?"PAUSE":"RUN",ORANGE_TONE,!sim_done)) {
        if(!sim_running) sim_running=1; else sim_running=0;
    }
    if(ui_button((Rectangle){890,463,155,42},"RESET",PURPLE_TONE,1)) reset_jobs();
    DrawText(TextFormat("SIMULATED CPU TIME: %d",clock_tick),733,535,12,TXT);
    DrawText(sim_done?"All threads completed":sim_running?"Scheduler running":"Scheduler paused",
             733,565,12,sim_done?GREEN_TONE:CYAN_TONE);
    draw_job_rows(priority);
}
static void draw_timeline(void) {
    title("Execution Timeline","Each colored block represents one CPU time unit; the sequence follows the selected scheduler.");
    panel((Rectangle){36,245,1049,365});
    DrawText("CPU EXECUTION ORDER",58,266,13,TXT);
    int shown=timeline_count<80?timeline_count:80;
    for(int i=0;i<shown;i++) {
        int col=i%20,row=i/20;
        float x=60+col*49,y=310+row*62;
        DrawRectangleRounded((Rectangle){x,y,42,38},0.16f,5,tc[timeline[i]]);
        DrawText(TextFormat("T%d",timeline[i]+1),(int)x+11,(int)y+12,13,BG);
    }
    DrawText(TextFormat("TIME UNITS RECORDED: %d",timeline_count),60,575,12,MUTED);
    DrawText("Choose Round Robin or Priority, then run the scheduler to populate this timeline.",
             38,635,12,MUTED);
}
static void draw_race(void) {
    title("Race Condition Demonstration","Two native worker threads each increment a shared counter 500 times.");
    panel((Rectangle){36,250,1049,310});
    DrawText("SHARED COUNTER",62,276,13,MUTED);
    mutex_lock(&race_mutex); int shown_counter=race_counter; mutex_unlock(&race_mutex);
    mutex_lock(&race_state_mutex); int shown_finished=race_finished; mutex_unlock(&race_state_mutex);
    DrawText(TextFormat("%d",shown_counter),62,310,54,race_safe?GREEN_TONE:CYAN_TONE);
    DrawText(TextFormat("EXPECTED RESULT: %d",race_expected),64,390,14,TXT);
    DrawText(race_started?(shown_finished>=2?"Workers finished":"Workers running"):
             "Choose a mode and start the workers.",64,423,13,MUTED);
    DrawText("UNPROTECTED LOGICAL UPDATE",620,295,13,ORANGE_TONE);
    DrawText("Read and write are separate operations.",620,322,12,MUTED);
    DrawText("Two workers can read the same old value,",620,344,12,MUTED);
    DrawText("then overwrite one another's increment.",620,366,12,MUTED);
    DrawText("MUTEX-PROTECTED UPDATE",620,415,13,GREEN_TONE);
    DrawText("The complete read-modify-write operation",620,442,12,MUTED);
    DrawText("is guarded by one mutex.",620,464,12,MUTED);
    if(ui_button((Rectangle){60,500,210,42},"RUN LOST-UPDATE DEMO",ORANGE_TONE,!race_started)) {
        race_safe=0; race_logical_mode=1; race_counter=0; race_finished=0;
        race_started=1; race_expected=1000;
        for(int i=0;i<2;i++) {
#ifdef _WIN32
            race_handles_valid[i]=thread_create(&race_handles[i],(ThreadFn)race_worker,(void*)(size_t)i);
#else
            race_handles_valid[i]=thread_create(&race_handles[i],race_worker,(void*)(size_t)i);
#endif
        }
    }
    if(ui_button((Rectangle){290,500,210,42},"RUN WITH MUTEX",GREEN_TONE,!race_started)) {
        race_safe=1; race_logical_mode=0; race_counter=0; race_finished=0;
        race_started=1; race_expected=1000;
        for(int i=0;i<2;i++) {
#ifdef _WIN32
            race_handles_valid[i]=thread_create(&race_handles[i],(ThreadFn)race_worker,(void*)(size_t)i);
#else
            race_handles_valid[i]=thread_create(&race_handles[i],race_worker,(void*)(size_t)i);
#endif
        }
    }
    if(ui_button((Rectangle){520,500,120,42},"RESET",PURPLE_TONE,1)) {
        for(int i=0;i<2;i++) if(race_handles_valid[i]) {
            thread_join(race_handles[i]); race_handles_valid[i]=0;
        }
        race_started=0; race_finished=0; race_counter=0;
    }
    if(race_started && shown_finished>=2) {
        for(int i=0;i<2;i++) if(race_handles_valid[i]) {
            thread_join(race_handles[i]); race_handles_valid[i]=0;
        }
        race_started=0;
    }
}
static void draw_deadlock(void) {
    title("Deadlock Demonstration","A circular wait occurs when each thread holds one resource and waits for the other.");
    panel((Rectangle){36,250,1049,350});
    /* Thread/resource relationship diagram. */
    DrawCircle(270,385,62,deadlock_active?RED_TONE:CYAN_TONE);
    DrawCircle(825,385,62,deadlock_active?RED_TONE:PURPLE_TONE);
    DrawText("THREAD 1",230,378,16,BG); DrawText("THREAD 2",785,378,16,BG);
    DrawRectangleRounded((Rectangle){485,300,130,58},0.18f,8,ORANGE_TONE);
    DrawRectangleRounded((Rectangle){485,445,130,58},0.18f,8,GREEN_TONE);
    DrawText("RESOURCE A",495,322,13,BG); DrawText("RESOURCE B",495,467,13,BG);
    DrawLineEx((Vector2){325,350},(Vector2){480,328},4,ORANGE_TONE);
    DrawLineEx((Vector2){620,328},(Vector2){770,350},4,ORANGE_TONE);
    DrawLineEx((Vector2){770,420},(Vector2){620,475},4,RED_TONE);
    DrawLineEx((Vector2){480,475},(Vector2){325,420},4,RED_TONE);
    DrawText("holds A",365,312,11,MUTED); DrawText("waits for B",650,312,11,MUTED);
    DrawText("holds B",650,487,11,MUTED); DrawText("waits for A",360,487,11,MUTED);
    if(deadlock_active) {
        DrawText("DEADLOCK: circular wait detected",375,535,15,RED_TONE);
    } else if(deadlock_prevented) {
        DrawText("PREVENTION: resource ordering breaks the circular wait",285,535,14,GREEN_TONE);
    } else {
        DrawText("No deadlock currently active",420,535,14,MUTED);
    }
    if(ui_button((Rectangle){60,625,220,42},"CREATE DEADLOCK",RED_TONE,!deadlock_active)) {
        deadlock_active=1; deadlock_prevented=0;
    }
    if(ui_button((Rectangle){300,625,220,42},"PREVENT DEADLOCK",GREEN_TONE,deadlock_active)) {
        deadlock_active=0; deadlock_prevented=1;
    }
    if(ui_button((Rectangle){540,625,150,42},"RESET",PURPLE_TONE,1)) {
        deadlock_active=0; deadlock_prevented=0;
    }
}

static void draw_producer_consumer(void) {
    title("Producer-Consumer with Semaphores",
          "A bounded buffer uses empty, full, and mutex synchronization to coordinate two native threads.");
    panel((Rectangle){36,245,1049,420});
    DrawText("SHARED BUFFER  /  CAPACITY 5",60,265,13,TXT);
    mutex_lock(&pc_mutex);
    int count=pc_count, produced=pc_produced, consumed=pc_consumed;
    int snapshot[BUFFER_SIZE];
    for(int i=0;i<BUFFER_SIZE;i++) snapshot[i]=pc_buffer[i];
    int log_count=pc_log_count, log_copy[PC_LOG_CAP];
    for(int i=0;i<log_count;i++) log_copy[i]=pc_log[i];
    int finished=pc_finished;
    mutex_unlock(&pc_mutex);

    for(int i=0;i<BUFFER_SIZE;i++) {
        float x=60+i*112;
        DrawRectangleRounded((Rectangle){x,310,96,72},0.12f,6,
                             (i<count)?PANEL2:(Color){35,45,65,255});
        DrawText((i<count)?TextFormat("%d",snapshot[i]):"EMPTY",
                 (int)x+24,331,18,(i<count)?CYAN_TONE:MUTED);
    }
    DrawText(TextFormat("ITEMS IN BUFFER: %d / %d",count,BUFFER_SIZE),60,400,13,TXT);
    DrawText(TextFormat("PRODUCED: %d / %d",produced,PC_ITEMS),60,430,13,GREEN_TONE);
    DrawText(TextFormat("CONSUMED: %d / %d",consumed,PC_ITEMS),300,430,13,ORANGE_TONE);
    DrawText(pc_started?(finished>=2?"Both threads completed":"Producer and consumer running"):
             "Press START to launch both threads.",60,466,12,MUTED);

    DrawText("SYNCHRONIZATION",650,265,13,TXT);
    DrawText("empty = available buffer slots",650,300,12,CYAN_TONE);
    DrawText("full  = items ready to consume",650,326,12,GREEN_TONE);
    DrawText("mutex = exclusive buffer access",650,352,12,ORANGE_TONE);
    DrawText("EVENT LOG  (+ produced / - consumed)",60,515,12,TXT);
    for(int i=0;i<log_count;i++) {
        int col=i%8, row=i/8;
        int value=log_copy[i];
        DrawRectangleRounded((Rectangle){60+col*118,545+row*29,105,23},0.15f,5,
                             value>0?GREEN_TONE:ORANGE_TONE);
        DrawText(TextFormat("%s %d",value>0?"PUT":"GET",abs(value)),
                 69+col*118,550+row*29,10,BG);
    }
    if(ui_button((Rectangle){760,610,135,40},"START",GREEN_TONE,!pc_started))
    {
        mutex_lock(&pc_mutex);
        pc_head=pc_tail=pc_count=pc_log_count=0;
        pc_produced=pc_consumed=pc_finished=0;
        memset(pc_buffer,0,sizeof(pc_buffer));
        mutex_unlock(&pc_mutex);
        pc_started=1;
#ifdef _WIN32
        pc_handles_valid[0]=thread_create(&pc_handles[0],(ThreadFn)producer_worker,NULL);
        pc_handles_valid[1]=thread_create(&pc_handles[1],(ThreadFn)consumer_worker,NULL);
#else
        pc_handles_valid[0]=thread_create(&pc_handles[0],producer_worker,NULL);
        pc_handles_valid[1]=thread_create(&pc_handles[1],consumer_worker,NULL);
#endif
        if(!pc_handles_valid[0] || !pc_handles_valid[1]) {
            /* A partial launch is joined during shutdown; report the failure in the status. */
            pc_finished=2;
        }
    }
    if(ui_button((Rectangle){915,610,135,40},"RESET",PURPLE_TONE,!pc_started)) {
        mutex_lock(&pc_mutex);
        pc_head=pc_tail=pc_count=pc_log_count=0;
        pc_produced=pc_consumed=pc_finished=0;
        memset(pc_buffer,0,sizeof(pc_buffer));
        mutex_unlock(&pc_mutex);
    }
    if(pc_started && finished>=2) {
        for(int i=0;i<2;i++) if(pc_handles_valid[i]) {
            thread_join(pc_handles[i]); pc_handles_valid[i]=0;
        }
        pc_started=0;
    }
}

static void draw_thread_time(void) {
    title("Individual Thread Execution Time",
          "Launch five native threads and measure each worker's elapsed wall-clock time independently.");
    panel((Rectangle){36,245,1049,390});
    DrawText("THREAD",60,270,12,MUTED);
    DrawText("WORKLOAD",245,270,12,MUTED);
    DrawText("ELAPSED TIME",500,270,12,MUTED);
    DrawText("CHECKSUM",760,270,12,MUTED);
    mutex_lock(&timing_mutex);
    int finished=timing_finished;
    TimingJob snapshot[NTHREADS];
    for(int i=0;i<NTHREADS;i++) snapshot[i]=timing_jobs[i];
    mutex_unlock(&timing_mutex);
    for(int i=0;i<NTHREADS;i++) {
        float y=305+i*52;
        panel((Rectangle){58,y,1000,42});
        DrawCircle(78,y+21,5,tc[i]);
        DrawText(tn[i],96,(int)y+14,12,TXT);
        DrawText(TextFormat("%llu operations",(unsigned long long)snapshot[i].iterations),
                 245,(int)y+14,11,MUTED);
        if(snapshot[i].done)
            DrawText(TextFormat("%.4f seconds",snapshot[i].elapsed),500,(int)y+13,12,GREEN_TONE);
        else
            DrawText("Waiting...",500,(int)y+13,12,MUTED);
        if(snapshot[i].done)
            DrawText(TextFormat("%llu",(unsigned long long)snapshot[i].checksum),
                     760,(int)y+13,11,CYAN_TONE);
    }
    DrawText(timing_started?(finished==NTHREADS?"All threads completed":"Workers running..."):
             "Click RUN to measure five independent worker threads.",60,590,12,MUTED);
    if(ui_button((Rectangle){790,575,125,42},"RUN",GREEN_TONE,!timing_started)) {
        mutex_lock(&timing_mutex);
        timing_finished=0;
        for(int i=0;i<NTHREADS;i++) {
            timing_jobs[i].id=i;
            timing_jobs[i].iterations=UINT64_C(3000000)+(uint64_t)i*UINT64_C(1500000);
            timing_jobs[i].elapsed=0.0;
            timing_jobs[i].checksum=0;
            timing_jobs[i].done=0;
        }
        mutex_unlock(&timing_mutex);
        timing_started=1;
        for(int i=0;i<NTHREADS;i++) {
#ifdef _WIN32
            timing_handles_valid[i]=thread_create(&timing_handles[i],(ThreadFn)timing_worker,&timing_jobs[i]);
#else
            timing_handles_valid[i]=thread_create(&timing_handles[i],timing_worker,&timing_jobs[i]);
#endif
            if(!timing_handles_valid[i]) {
                mutex_lock(&timing_mutex); timing_finished++; mutex_unlock(&timing_mutex);
            }
        }
    }
    if(ui_button((Rectangle){930,575,125,42},"RESET",PURPLE_TONE,!timing_started)) {
        mutex_lock(&timing_mutex);
        timing_finished=0;
        for(int i=0;i<NTHREADS;i++) {
            timing_jobs[i].elapsed=0.0; timing_jobs[i].checksum=0;
            timing_jobs[i].done=0;
        }
        mutex_unlock(&timing_mutex);
    }
    if(timing_started && finished>=NTHREADS) {
        for(int i=0;i<NTHREADS;i++) if(timing_handles_valid[i]) {
            thread_join(timing_handles[i]); timing_handles_valid[i]=0;
        }
        timing_started=0;
    }
}

static void draw_home(void) {
    title("ThreadLab Feature Hub","Explore CPU scheduling, synchronization, deadlocks, and thread execution.");
    const char *names[]={"ROUND ROBIN SCHEDULING","PRIORITY SCHEDULING","RACE CONDITION DEMO",
                         "DEADLOCK DEMO","EXECUTION TIMELINE",
                         "PRODUCER-CONSUMER SEMAPHORES","INDIVIDUAL THREAD EXECUTION TIME"};
    const char *desc[]={"Time-slice rotation and configurable quantum",
                        "Run ready threads by priority level",
                        "Compare lost updates with mutex protection",
                        "Explore circular wait and prevention",
                        "Visual Gantt-style CPU execution history",
                        "Bounded buffer using empty, full, and mutex semaphores",
                        "Measure elapsed time and checksum for each native worker"};
    for(int i=0;i<7;i++) {
        float y=235+i*64;
        panel((Rectangle){38,y,1045,61});
        DrawCircle(64,y+30,6,tc[i]);
        DrawText(names[i],84,(int)y+10,14,TXT);
        DrawText(desc[i],84,(int)y+34,11,MUTED);
        DrawText("OPEN  >",965,(int)y+22,12,CYAN_TONE);
        if(CheckCollisionPointRec(GetMousePosition(),(Rectangle){38,y,1045,61}) &&
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const Tab destinations[]={TAB_RR,TAB_PRIORITY,TAB_RACE,TAB_DEADLOCK,
                                       TAB_TIMELINE,TAB_PRODUCER_CONSUMER,TAB_THREAD_TIME};
            tab=destinations[i];
            if(tab==TAB_RR || tab==TAB_PRIORITY) reset_jobs();
        }
    }
}
static THREAD_RETURN race_worker(THREAD_ARG arg) {
    (void)arg;
    for(int n=0;n<500;n++) {
        if(race_safe) {
            mutex_lock(&race_mutex);
            race_counter++;
            mutex_unlock(&race_mutex);
        } else {
            int old;
            /* Lock individual accesses to avoid a C data race, but deliberately
               leave the read-modify-write sequence non-atomic. */
            mutex_lock(&race_mutex); old=race_counter; mutex_unlock(&race_mutex);
            sleep_ms(1);
            mutex_lock(&race_mutex); race_counter=old+1; mutex_unlock(&race_mutex);
        }
    }
    mutex_lock(&race_state_mutex); race_finished++; mutex_unlock(&race_state_mutex);
    return THREAD_DONE;
}


static THREAD_RETURN producer_worker(THREAD_ARG arg) {
    (void)arg;
    for(int item=1;item<=PC_ITEMS;item++) {
        semaphore_wait(&pc_empty);
        mutex_lock(&pc_mutex);
        pc_buffer[pc_tail]=item;
        pc_tail=(pc_tail+1)%BUFFER_SIZE;
        pc_count++;
        pc_produced++;
        pc_log_event(item,1);
        mutex_unlock(&pc_mutex);
        semaphore_post(&pc_full);
        sleep_ms(250);
    }
    mutex_lock(&pc_mutex); pc_finished++; mutex_unlock(&pc_mutex);
    return THREAD_DONE;
}

static THREAD_RETURN consumer_worker(THREAD_ARG arg) {
    (void)arg;
    for(int n=0;n<PC_ITEMS;n++) {
        semaphore_wait(&pc_full);
        mutex_lock(&pc_mutex);
        int item=pc_buffer[pc_head];
        pc_head=(pc_head+1)%BUFFER_SIZE;
        pc_count--;
        pc_consumed++;
        pc_log_event(item,0);
        mutex_unlock(&pc_mutex);
        semaphore_post(&pc_empty);
        sleep_ms(420);
    }
    mutex_lock(&pc_mutex); pc_finished++; mutex_unlock(&pc_mutex);
    return THREAD_DONE;
}

static THREAD_RETURN timing_worker(THREAD_ARG arg) {
    TimingJob *job=(TimingJob*)arg;
    double start=monotonic_seconds();
    volatile uint64_t sum=0;
    for(uint64_t n=0;n<job->iterations;n++)
        sum += (n % 97u) * (uint64_t)(job->id+1);
    double elapsed=monotonic_seconds()-start;
    mutex_lock(&timing_mutex);
    job->checksum=(uint64_t)sum;
    job->elapsed=elapsed;
    job->done=1;
    timing_finished++;
    mutex_unlock(&timing_mutex);
    return THREAD_DONE;
}

int main(void) {
    mutex_init(&race_mutex); mutex_init(&race_state_mutex);
    mutex_init(&pc_mutex); mutex_init(&timing_mutex);
    semaphore_init(&pc_empty,BUFFER_SIZE,BUFFER_SIZE);
    semaphore_init(&pc_full,0,BUFFER_SIZE);
    reset_jobs();
    SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT);
    InitWindow(W,H,"ThreadLab - Multithreading Simulator");
    SetWindowMinSize(1120,760); SetTargetFPS(60);
    while(!WindowShouldClose()) {
        if(sim_running && (tab==TAB_RR || tab==TAB_PRIORITY)) {
            tick_accum++;
            if(tick_accum>=24) { tick_accum=0; scheduler_step(); }
        }
        BeginDrawing(); ClearBackground(BG); header(); tabs();
        switch(tab) {
            case TAB_HOME: draw_home(); break;
            case TAB_RR: draw_scheduler(0); break;
            case TAB_PRIORITY: draw_scheduler(1); break;
            case TAB_RACE: draw_race(); break;
            case TAB_DEADLOCK: draw_deadlock(); break;
            case TAB_TIMELINE: draw_timeline(); break;
            case TAB_PRODUCER_CONSUMER: draw_producer_consumer(); break;
            case TAB_THREAD_TIME: draw_thread_time(); break;
        }
        DrawLine(34,720,1086,720,(Color){54,67,91,255});
        DrawText("C11  /  RAYLIB  /  NATIVE THREADS",36,733,10,MUTED);
        DrawText("MACOS + WINDOWS",930,733,10,CYAN_TONE);
        EndDrawing();
    }
    for(int i=0;i<2;i++) if(race_handles_valid[i]) thread_join(race_handles[i]);
    for(int i=0;i<2;i++) if(pc_handles_valid[i]) thread_join(pc_handles[i]);
    for(int i=0;i<NTHREADS;i++) if(timing_handles_valid[i]) thread_join(timing_handles[i]);
    CloseWindow();
    semaphore_destroy(&pc_empty); semaphore_destroy(&pc_full);
    mutex_destroy(&pc_mutex); mutex_destroy(&timing_mutex);
    mutex_destroy(&race_mutex); mutex_destroy(&race_state_mutex);
    return 0;
}
