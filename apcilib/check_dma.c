/* TODO: Make sure FIFO_SIZE is correctly autodetecting */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "apcilib.h"

#define DEFAULT_DEVICEPATH "/dev/apci/pcie_adio16_16fds_0"
#define FALLBACK_DEVICEPATH "/dev/apci/mpcie_adio16_8f_0"

#define BAR_REGISTER 1
#define SAMPLE_RATE 10000.0
#define LOG_FILE_NAME "samples.csv"
#define SECONDS_TO_LOG 2.0

static uint8_t CHANNEL_COUNT = 8;
#define HIGH_CHANNEL (CHANNEL_COUNT - 1)
#define NUM_CHANNELS (2 * CHANNEL_COUNT)
#define AMOUNT_OF_SAMPLES_TO_LOG (SECONDS_TO_LOG * SAMPLE_RATE * 2)

#define FIFO_SIZE 0xF00
#define SAMPLES_PER_FIFO_ENTRY 2
#define BYTES_PER_FIFO_ENTRY (4 * SAMPLES_PER_FIFO_ENTRY)
#define BYTES_PER_TRANSFER (FIFO_SIZE * BYTES_PER_FIFO_ENTRY)
#define SAMPLES_PER_TRANSFER (FIFO_SIZE * SAMPLES_PER_FIFO_ENTRY)

#define RESETOFFSET 0x00
#define BASECLOCKOFFSET 0x0C
#define DIVISOROFFSET 0x10
#define ADCRANGEOFFSET 0x18
#define FAFIRQTHRESHOLDOFFSET 0x20
#define ADCCONTROLOFFSET 0x38
#define IRQENABLEOFFSET 0x40
#define ADC_START_MASK 0x30000

#define RING_BUFFER_SLOTS 4
#define DMA_BUFF_SIZE (BYTES_PER_TRANSFER * RING_BUFFER_SLOTS)
#define NUMBER_OF_DMA_TRANSFERS ((__u32)((AMOUNT_OF_SAMPLES_TO_LOG + SAMPLES_PER_TRANSFER - 1) / SAMPLES_PER_TRANSFER))

#define bmADIO_DMADoneEnable (1 << 2)
#define bmADIO_ADCTRIGGEREnable (1 << 0)

static int fd = -1;
static pthread_t logger_thread;
static pthread_t worker_thread;
static sem_t ring_sem;
static pthread_mutex_t ring_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t terminate;
static uint32_t ring_buffer[RING_BUFFER_SLOTS][SAMPLES_PER_TRANSFER];
static int ring_read_index;
static int ring_write_index;
static int queued_buffers;

static void stop_acquisition(void)
{
    uint32_t dummy;

    if (fd >= 0) {
        apci_write32(fd, 1, BAR_REGISTER, IRQENABLEOFFSET, 0);
        apci_read32(fd, 1, BAR_REGISTER, IRQENABLEOFFSET, &dummy);
    }
}

static void abort_handler(int s)
{
    printf("Caught signal %d\n", s);
    terminate = 2;
    stop_acquisition();
    sem_post(&ring_sem);
}

static int push_ring_buffer(const void *src)
{
    pthread_mutex_lock(&ring_mutex);

    if (queued_buffers >= RING_BUFFER_SLOTS) {
        pthread_mutex_unlock(&ring_mutex);
        return -1;
    }

    memcpy(ring_buffer[ring_write_index], src, BYTES_PER_TRANSFER);
    ring_write_index = (ring_write_index + 1) % RING_BUFFER_SLOTS;
    queued_buffers++;

    pthread_mutex_unlock(&ring_mutex);
    sem_post(&ring_sem);
    return 0;
}

static int pop_ring_buffer(uint32_t **buffer)
{
    pthread_mutex_lock(&ring_mutex);

    if (queued_buffers <= 0) {
        pthread_mutex_unlock(&ring_mutex);
        return 0;
    }

    *buffer = ring_buffer[ring_read_index];
    ring_read_index = (ring_read_index + 1) % RING_BUFFER_SLOTS;
    queued_buffers--;

    pthread_mutex_unlock(&ring_mutex);
    return 1;
}

static void *log_main(void *arg)
{
    int samples = 0;

    (void)arg;
    FILE *out = fopen(LOG_FILE_NAME, "w");

    if (out == NULL) {
        perror(LOG_FILE_NAME);
        terminate = 2;
        return NULL;
    }

    while (1) {
        uint32_t *buffer;

        sem_wait(&ring_sem);

        if (pop_ring_buffer(&buffer) == 0) {
            if (terminate)
                break;
            continue;
        }

        for (int ii = 0; ii < SAMPLES_PER_TRANSFER; ii += NUM_CHANNELS) {
            ++samples;
            for (int jj = 0; jj < NUM_CHANNELS; ++jj) {
                int16_t dval = buffer[ii + jj] & 0xFFFF;
                fprintf(out, "%d=%d,", (buffer[ii + jj] >> 20) & 0xF, dval);
            }
            fprintf(out, "\n");
        }
    }

    fflush(out);
    fclose(out);

    printf("Recorded %d samples on %d channels at rate %f\n", samples, NUM_CHANNELS, SAMPLE_RATE);
    printf("Duration: %f\n", (CHANNEL_COUNT / SAMPLE_RATE) * samples);
    return NULL;
}

static void *worker_main(void *arg)
{
    __u32 transfer_count = 0;

    (void)arg;
    void *mmap_addr = mmap(NULL, DMA_BUFF_SIZE, PROT_READ, MAP_SHARED, fd, 0);

    if (mmap_addr == MAP_FAILED) {
        perror("mmap");
        terminate = 2;
        sem_post(&ring_sem);
        return NULL;
    }

    while (!terminate && transfer_count < NUMBER_OF_DMA_TRANSFERS) {
        int first_slot;
        int num_slots;
        int data_discarded;
        int status = apci_dma_data_ready(fd, 1, &first_slot, &num_slots, &data_discarded);

        if (status) {
            printf("apci_dma_data_ready failed: %d\n", status);
            terminate = 2;
            break;
        }

        if (data_discarded != 0)
            printf("Warning: driver discarded %d DMA buffer%c\n", data_discarded, data_discarded == 1 ? ' ' : 's');

        if (num_slots == 0) {
            status = apci_wait_for_irq(fd, 1);
            if (status) {
                printf("apci_wait_for_irq failed: %d\n", status);
                terminate = 2;
                break;
            }
            continue;
        }

        for (int i = 0; i < num_slots && transfer_count < NUMBER_OF_DMA_TRANSFERS; ++i) {
            int dma_slot = (first_slot + i) % RING_BUFFER_SLOTS;
            void *src = (char *)mmap_addr + (BYTES_PER_TRANSFER * dma_slot);

            if (push_ring_buffer(src) != 0) {
                printf("Ring buffer overrun. Saving the log was too slow. Aborting.\n");
                terminate = 2;
                break;
            }

            transfer_count++;
        }

        apci_dma_data_done(fd, 1, num_slots);
    }

    munmap(mmap_addr, DMA_BUFF_SIZE);
    terminate = terminate ? terminate : 1;
    sem_post(&ring_sem);
    return NULL;
}

static void set_acquisition_rate(int fd, double *Hz)
{
    uint32_t base_clock;
    uint32_t divisor;

    apci_read32(fd, 1, BAR_REGISTER, BASECLOCKOFFSET, &base_clock);
    divisor = round(base_clock / *Hz);
    *Hz = base_clock / divisor;

    apci_write32(fd, 1, BAR_REGISTER, DIVISOROFFSET, divisor);
    divisor = divisor / CHANNEL_COUNT;
    apci_write32(fd, 1, BAR_REGISTER, DIVISOROFFSET + 4, divisor);
}

static int open_apci_device(const char *requested_path)
{
    fd = open(requested_path, O_RDWR);
    if (fd >= 0)
        return 0;

    if (strcmp(requested_path, DEFAULT_DEVICEPATH) != 0)
        return -1;

    fd = open(FALLBACK_DEVICEPATH, O_RDWR);
    if (fd >= 0)
        return 0;

    return -1;
}

int main(int argc, char **argv)
{
    int status;
    double rate = SAMPLE_RATE;
    uint32_t depth_readback;
    uint32_t start_command;
    time_t timerStart;
    time_t timerEnd;
    char buffer[30];
    struct tm tm_info;
    struct sigaction sigIntHandler;
    const char *devpath = (argc > 1) ? argv[1] : DEFAULT_DEVICEPATH;

    terminate = 0;
    ring_read_index = 0;
    ring_write_index = 0;
    queued_buffers = 0;

    sigIntHandler.sa_handler = abort_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);
    sigaction(SIGABRT, &sigIntHandler, NULL);

    printf("mPCIe-ADIO16-16F Family ADC logging sample.\n");

    if (open_apci_device(devpath) != 0) {
        perror(devpath);
        return 1;
    }

    sem_init(&ring_sem, 0, 0);

    status = apci_dma_transfer_size(fd, 1, RING_BUFFER_SLOTS, BYTES_PER_TRANSFER);
    if (status) {
        printf("Error setting DMA transfer size: %d\n", status);
        close(fd);
        return 1;
    }

    apci_write32(fd, 1, BAR_REGISTER, FAFIRQTHRESHOLDOFFSET, FIFO_SIZE);
    apci_read32(fd, 1, BAR_REGISTER, FAFIRQTHRESHOLDOFFSET, &depth_readback);
    printf("FIFO Almost Full IRQ threshold readback: 0x%x\n", depth_readback);

    set_acquisition_rate(fd, &rate);
    printf("ADC Rate: %lf Hz\n", rate);

    apci_write32(fd, 1, BAR_REGISTER, ADCRANGEOFFSET, 0);
    apci_write32(fd, 1, BAR_REGISTER, IRQENABLEOFFSET, bmADIO_ADCTRIGGEREnable | bmADIO_DMADoneEnable);

    pthread_create(&worker_thread, NULL, &worker_main, NULL);
    pthread_create(&logger_thread, NULL, &log_main, NULL);

    start_command = 0x7f4ee;
    start_command &= ~(7 << 12);
    start_command |= (HIGH_CHANNEL & 0x06) << 12;
    start_command |= ADC_START_MASK;

    timerStart = time(NULL);
    apci_write32(fd, 1, BAR_REGISTER, ADCCONTROLOFFSET, start_command);

    pthread_join(worker_thread, NULL);
    stop_acquisition();
    pthread_join(logger_thread, NULL);

    timerEnd = time(NULL);

    strftime(buffer, sizeof buffer, "Start: %Y-%m-%d %H:%M:%S", localtime_r(&timerStart, &tm_info));
    puts(buffer);
    strftime(buffer, sizeof buffer, "End:   %Y-%m-%d %H:%M:%S", localtime_r(&timerEnd, &tm_info));
    puts(buffer);
    printf("Finished recording in %ld seconds\n", timerEnd - timerStart);
    printf("Done. Data logged to %s\n", LOG_FILE_NAME);

    sem_destroy(&ring_sem);
    close(fd);
    return terminate == 2 ? 1 : 0;
}
