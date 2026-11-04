#include "AIOAIO.h"
#include "backend.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using namespace aioaio;
constexpr uint32_t kDMASlots = 4;
constexpr uint32_t kQueueSlots = 16;

uint64_t ns_since(Clock::time_point t) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count());
}

template<class F> TWinErrCode checked(F &&f) noexcept {
    try { return static_cast<TWinErrCode>(f()); }
    catch (const std::bad_alloc &) { return ENOMEM; }
    catch (const std::system_error &e) { return e.code().value() > 0 ? e.code().value() : EIO; }
    catch (...) { return EIO; }
}

struct Board {
    std::unique_ptr<Backend> hw;
    Config config;
    double actual_rate = 10000;
    std::mutex control, data;
    std::condition_variable changed;
    std::thread worker;
    std::atomic<bool> stopping{false};
    bool closed = false, worker_done = true;
    uint32_t rows = 1024;
    std::vector<uint32_t> queue;
    size_t head = 0, count = 0;
    TADCAcquisitionStatus status{};
    Clock::time_point started{};

    explicit Board(std::unique_ptr<Backend> h) : hw(std::move(h)) {
        config.channels_per_adc = hw->model().channels_per_adc;
        config.range_codes.fill(7); // bipolar 10 V
        rows = std::min(rows, hw->model().max_fifo_rows);
        status.StructSize = sizeof(status);
        status.State = AIO_STATE_IDLE;
    }
    ~Board() { stop(); }

    // control mutex must be held by callers; the destructor is exclusive.
    int stop() noexcept {
        stopping.store(true);
        for (;;) {
            std::unique_lock<std::mutex> lock(data);
            if (worker_done) break;
            lock.unlock();
            // Repeating cancellation closes the check-before-wait race in the
            // existing APCI ioctl. EALREADY simply means no wait is active yet.
            try { (void)hw->cancel_irq(); } catch (...) {}
            lock.lock();
            changed.wait_for(lock, std::chrono::milliseconds(1), [&]{ return worker_done; });
        }
        if (worker.joinable()) worker.join();
        std::lock_guard<std::mutex> lock(data);
        return status.State == AIO_STATE_FAILED ? static_cast<int>(status.ErrorCode) : 0;
    }

    bool running() {
        std::lock_guard<std::mutex> lock(data);
        return !worker_done;
    }
    int empty_result() const {
        switch (status.State) {
        case AIO_STATE_RUNNING: return EAGAIN;
        case AIO_STATE_FAILED: return status.ErrorCode ? static_cast<int>(status.ErrorCode) : EIO;
        case AIO_STATE_CANCELLED: return ECANCELED;
        default: return ENODATA;
        }
    }

    void acquire(uint64_t target, uint32_t slot_rows) noexcept {
        int error = 0;
        bool complete = false;
        const size_t words = static_cast<size_t>(slot_rows) * hw->model().raw_words_per_row;
        try {
            while (!stopping.load() && !complete) {
                uint32_t first = 0, slots = 0, discarded = 0;
                error = hw->ready(first, slots, discarded);
                if (error) break;
                if (discarded) {
                    std::lock_guard<std::mutex> lock(data);
                    status.DiscardedDMASlots += discarded;
                    error = EOVERFLOW;
                    break;
                }
                if (slots > kDMASlots || (slots && first >= kDMASlots)) { error = EPROTO; break; }
                if (!slots) {
                    if (stopping.load()) break;
                    error = hw->wait_irq();
                    if (error == EINTR) { error = 0; continue; }
                    if (stopping.load() && (error == ECANCELED || error == EALREADY)) error = 0;
                    if (error) break;
                    continue;
                }
                uint32_t consumed = 0;
                for (; consumed < slots && !complete && !stopping.load(); ++consumed) {
                    const uint32_t *raw = hw->slot((first + consumed) % kDMASlots);
                    if (!raw) { error = EFAULT; break; }
                    {
                        std::lock_guard<std::mutex> lock(data);
                        ++status.DMACompletions;
                        status.DMABytes += words * sizeof(uint32_t);
                        for (size_t i = 0; i < words; ++i) {
                            // Native FPGA marks invalid FIFO words with bit 31.
                            // Keep each valid conversion/status word in wire order.
                            if (raw[i] & 0x80000000u) continue;
                            if (count == queue.size()) { error = ENOBUFS; break; }
                            queue[(head + count) % queue.size()] = raw[i];
                            ++count;
                            ++status.CapturedSamples;
                            if (!status.FirstDataNanoseconds) status.FirstDataNanoseconds = ns_since(started);
                            if (target && status.CapturedSamples == target) { complete = true; break; }
                        }
                    }
                    changed.notify_all();
                    if (error) { ++consumed; break; }
                }
                if (consumed) {
                    const int ack_error = hw->acknowledge(consumed);
                    if (!error) error = ack_error;
                }
                if (error) break;
            }
        } catch (const std::bad_alloc &) { error = ENOMEM; }
          catch (...) { error = EIO; }
        try {
            const int stop_error = hw->stop_adc();
            if (!error) error = stop_error;
        } catch (...) { if (!error) error = EIO; }
        {
            std::lock_guard<std::mutex> lock(data);
            status.ErrorCode = error;
            status.State = error ? AIO_STATE_FAILED : (complete ? AIO_STATE_COMPLETE : AIO_STATE_CANCELLED);
            status.ElapsedNanoseconds = ns_since(started);
            worker_done = true;
        }
        changed.notify_all();
    }
};

struct Registry {
    std::mutex mutex;
    std::vector<std::shared_ptr<Board>> boards;
    ~Registry() {
        for (auto &b : boards) if (b) { std::lock_guard<std::mutex> lock(b->control); b->stop(); }
    }
} registry;

std::shared_ptr<Board> get(TBoardIndex index) {
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (index >= registry.boards.size()) return {};
    return registry.boards[static_cast<size_t>(index)];
}

template<class F> int configure(TBoardIndex index, F &&change) {
    auto b = get(index);
    if (!b) return ENODEV;
    std::lock_guard<std::mutex> lock(b->control);
    if (b->closed) return ENODEV;
    if (b->running()) return EBUSY;
    Config next = b->config;
    int error = change(next, b->hw->model());
    if (error) return error;
    double actual = 0;
    error = b->hw->configure(next, actual);
    if (!error) { b->config = next; b->actual_rate = actual; }
    return error;
}

int range_code(uint32_t range, uint32_t &out) {
    switch (range) {
    case rcBipolar12: out=0; break;
    case rcBipolar5: out=1; break;
    case rcBipolar2_5: out=2; break;
    case rcBipolar1_25: out=3; break;
    case rcBipolar_625: out=4; break;
    case rcBipolar_3125: out=5; break;
    case rcBipolar10: out=7; break;
    default: if (range > 7 || range == 6) return EINVAL; out=range;
    }
    return 0;
}

int begin(TBoardIndex index, uint64_t samples) {
    auto b = get(index);
    if (!b) return ENODEV;
    std::lock_guard<std::mutex> lock(b->control);
    if (b->closed) return ENODEV;
    if (b->running()) return EBUSY;
    {
        std::lock_guard<std::mutex> qlock(b->data);
        if (b->count) return EBUSY; // caller must drain or explicitly clear old data
    }
    if (b->worker.joinable()) b->worker.join();
    const uint32_t row_words = b->hw->model().raw_words_per_row;
    if (!row_words || !b->rows) return EPROTO;
    uint32_t rows = b->rows;
    if (samples) rows = static_cast<uint32_t>(std::min<uint64_t>(rows, (samples+row_words-1)/row_words));
    b->queue.assign(std::max<size_t>(8192, static_cast<size_t>(rows)*row_words*kQueueSlots), 0);
    double actual = 0;
    int error = b->hw->configure(b->config, actual);
    if (error) return error;
    b->actual_rate = actual;
    error = b->hw->prepare_dma(kDMASlots, rows);
    if (error) return error;
    {
        std::lock_guard<std::mutex> qlock(b->data);
        b->head = b->count = 0;
        b->status = {};
        b->status.StructSize = sizeof(b->status);
        b->status.State = AIO_STATE_RUNNING;
        b->status.RequestedSamples = samples;
        b->status.ChannelCount = b->config.channels_per_adc * b->hw->model().adc_count;
        b->status.ActualRateHz = actual;
        b->stopping.store(false);
        b->worker_done = false;
        b->started = Clock::now();
    }
    try {
        error = b->hw->start_adc();
        if (!error)
            b->worker = std::thread([b,samples,rows]{ b->acquire(samples,rows); });
    } catch (const std::bad_alloc &) {
        error = ENOMEM;
    } catch (const std::system_error &e) {
        error = e.code().value() > 0 ? e.code().value() : EIO;
    } catch (...) {
        error = EIO;
    }
    if (error) {
        // Even a throwing backend must leave no phantom worker for Close or
        // AcquireEnd to wait on. Preserve the startup error if cleanup fails.
        try { (void)b->hw->stop_adc(); } catch (...) {}
        std::lock_guard<std::mutex> qlock(b->data);
        b->worker_done = true;
        b->status.State = AIO_STATE_FAILED;
        b->status.ErrorCode = error;
        b->status.ElapsedNanoseconds = ns_since(b->started);
        b->changed.notify_all();
    }
    return error;
}

int read_data(TBoardIndex index, uint32_t capacity, uint32_t *got,
              uint32_t *raw, uint16_t *counts, double *volts,
              uint16_t *status, uint16_t *timestamps) {
    if (!got) return EINVAL;
    *got=0;
    if (capacity && !raw && !counts && !volts && !status && !timestamps) return EINVAL;
    auto b=get(index);
    if (!b) return ENODEV;
    std::lock_guard<std::mutex> lock(b->data);
    if (!capacity) return 0;
    if (!b->count) return b->empty_result();
    const size_t n=std::min<size_t>(capacity,b->count);
    // ADAS3022 range spans include the documented 2.4% overrange margin.
    static constexpr double scales[8]={24.576/32768,10.24/32768,5.12/32768,2.56/32768,1.28/32768,.64/32768,0,20.48/32768};
    for (size_t i=0;i<n;++i) {
        const uint32_t word=b->queue[(b->head+i)%b->queue.size()];
        if (raw) raw[i]=word;
        if (counts) counts[i]=static_cast<uint16_t>(word);
        if (status) status[i]=static_cast<uint16_t>(word>>16);
        if (timestamps) timestamps[i]=0; // no hardware timestamp in this format
        if (volts) {
            const unsigned gain=(word>>16)&7;
            const int32_t signed_count=static_cast<int32_t>(word&0xffffu)-((word&0x8000u)?65536:0);
            volts[i]=gain==6 ? std::numeric_limits<double>::quiet_NaN() : signed_count*scales[gain];
        }
    }
    b->head=(b->head+n)%b->queue.size(); b->count-=n;
    b->status.DeliveredSamples+=n;
    *got=static_cast<uint32_t>(n);
    return 0;
}
}

extern "C" {
TWinErrCode BRD_Open(const char *path, TBoardIndex *out) {
    return checked([&]() -> int {
        if (!path || !*path || !out) return EINVAL;
        *out=biNone;
        std::lock_guard<std::mutex> lock(registry.mutex);
        for (size_t i=0;i<registry.boards.size();++i) if (registry.boards[i] && registry.boards[i]->hw->path()==path) { *out=i; return 0; }
        std::unique_ptr<Backend> backend;
        int error=open_backend(path,backend);
        if (error) return error;
        if (!backend) return EIO;
        auto b=std::make_shared<Board>(std::move(backend));
        const auto index=registry.boards.size();
        registry.boards.push_back(std::move(b));
        *out=index;
        return 0;
    });
}
TWinErrCode AIO_Initialize(void) {
    return checked([]() -> int {
        DIR *dir=opendir("/dev/apci");
        if (!dir) return errno;
        std::vector<std::string> paths;
        try { while (auto *e=readdir(dir)) if (e->d_name[0]!='.') paths.emplace_back(std::string("/dev/apci/")+e->d_name); }
        catch (...) { closedir(dir); throw; }
        closedir(dir); std::sort(paths.begin(),paths.end());
        int last_error=ENODEV; bool found=false;
        for (auto &path:paths) { TBoardIndex index; int error=BRD_Open(path.c_str(),&index); if (!error) found=true; else if (error!=ENODEV && error!=ENOTSUP) last_error=error; }
        return found?0:last_error;
    });
}
TWinErrCode BRD_Close(TBoardIndex index) {
    return checked([&]() -> int {
        auto b=get(index); if (!b) return ENODEV;
        int error;
        { std::lock_guard<std::mutex> lock(b->control); b->closed=true; error=b->stop(); }
        { std::lock_guard<std::mutex> lock(registry.mutex); if (index<registry.boards.size() && registry.boards[index]==b) registry.boards[index].reset(); }
        return error;
    });
}
TWinErrCode AIO_Shutdown(void) {
    return checked([]() -> int {
        std::vector<std::shared_ptr<Board>> old;
        { std::lock_guard<std::mutex> lock(registry.mutex); old.swap(registry.boards); }
        int error=0;
        for (auto &b:old) if(b) { std::lock_guard<std::mutex> lock(b->control); b->closed=true; int e=b->stop(); if(!error) error=e; }
        return error;
    });
}
const char *AIO_ErrorString(TWinErrCode error) { return std::strerror(static_cast<int>(error)); }
TBoardIndex BRD_GetHighestBoardIndex(void) {
    std::lock_guard<std::mutex> lock(registry.mutex);
    for(size_t i=registry.boards.size();i>0;--i) if(registry.boards[i-1]) return i-1;
    return biNone;
}
TWinErrCode BRD_GetCapabilities(TBoardIndex index,TBoardCapabilities *caps) {
    return checked([&]() -> int {
        // Preserve the original capability structure for existing libaioaio.so.1
        // callers; never write the appended rate field beyond their buffer.
        constexpr size_t original_size = offsetof(TBoardCapabilities, MaxADCRateHz);
        if(!caps || caps->StructSize<original_size) return EINVAL;
        const size_t output_size = caps->StructSize >= sizeof(*caps) ? sizeof(*caps) : original_size;
        auto b=get(index); if(!b) return ENODEV;
        const auto &m=b->hw->model(); TBoardCapabilities result{};
        result.StructSize=static_cast<uint32_t>(output_size);
        result.DeviceID=m.device_id; result.ADCCount=m.adc_count; result.ChannelsPerADC=m.channels_per_adc;
        result.MaxChannels=m.adc_count*m.channels_per_adc; result.ResolutionBits=m.resolution_bits;
        result.FIFORowBytes=m.raw_words_per_row*4; result.DMAAvailable=1;
        result.MaxADCRateHz=m.max_adc_rate_hz;
        std::snprintf(result.DevicePath,sizeof(result.DevicePath),"%s",b->hw->path().c_str());
        std::snprintf(result.Name,sizeof(result.Name),"%s",m.name);
        std::memcpy(caps,&result,output_size); return 0;
    });
}
TWinErrCode BRD_GetDeviceID(TBoardIndex index,TPUInt32 id) {
    return checked([&]() -> int { if(!id)return EINVAL; auto b=get(index); if(!b)return ENODEV; *id=b->hw->model().device_id; return 0; });
}
TWinErrCode BRD_GetNameA(TBoardIndex index,TUInt32 length,TPAnsiChar name,TPUInt32 used) {
    return checked([&]() -> int { auto b=get(index); if(!b)return ENODEV; const auto *s=b->hw->model().name; size_t n=std::strlen(s); if(used)*used=static_cast<uint32_t>(n); if(length && !name)return EINVAL; if(length)std::snprintf(name,length,"%s",s); return length && length<=n?ENOSPC:0; });
}
TWinErrCode BRD_GetNameW(TBoardIndex index,TUInt32 length,TPWideChar name,TPUInt32 used) {
    return checked([&]() -> int { auto b=get(index); if(!b)return ENODEV; const auto *s=b->hw->model().name; size_t n=std::strlen(s); if(used)*used=static_cast<uint32_t>(n); if(length&&!name)return EINVAL; if(length){size_t k=std::min<size_t>(n,length-1); for(size_t i=0;i<k;++i)name[i]=static_cast<unsigned char>(s[i]); name[k]=0;} return length&&length<=n?ENOSPC:0; });
}
TWinErrCode ADC_SetRate(TBoardIndex b,TDouble hz) {
    return checked([&]{ return configure(b,[&](Config &c,const Model &m) {
        if(!std::isfinite(hz)||hz<=0) return EINVAL;
        if(hz>m.max_adc_rate_hz) return ERANGE;
        c.rate_hz=hz; return 0;
    }); });
}
TWinErrCode ADC_GetRate(TBoardIndex index,TPDouble hz) {
    return checked([&]() -> int { if(!hz)return EINVAL; auto b=get(index); if(!b)return ENODEV; std::lock_guard<std::mutex> lock(b->control); if(b->closed)return ENODEV; if(!b->running()){double actual; int e=b->hw->configure(b->config,actual); if(e)return e; b->actual_rate=actual;} *hz=b->actual_rate; return 0; });
}
TWinErrCode ADC_SetChannelCount(TBoardIndex b,TChannelIndex n) { return checked([&]{ return configure(b,[&](Config &c,const Model &m) { uint32_t max=m.channels_per_adc/(c.differential?2:1); uint32_t total=n==ciAll?max*m.adc_count:n; if(!total||total>max*m.adc_count||total%m.adc_count)return EINVAL; c.channels_per_adc=total/m.adc_count; return 0; }); }); }
TWinErrCode ADC_SetDifferential(TBoardIndex b,TLongBool diff) { return checked([&]{ return configure(b,[&](Config &c,const Model &m) { c.differential=diff!=0; c.channels_per_adc=m.channels_per_adc/(c.differential?2:1); return 0; }); }); }
TWinErrCode ADC_SetRange1(TBoardIndex b,TChannelIndex channel,TRangeCode range) {
    return checked([&]{ return configure(b,[&](Config &c,const Model &m) { if(channel!=ciAll&&channel>=m.adc_count*m.channels_per_adc)return EINVAL; if(range==rcNoChange)return 0; uint32_t code; int e=range_code(range,code); if(e)return e; if(channel==ciAll)c.range_codes.fill(code); else c.range_codes[channel]=code; return 0; }); });
}
TWinErrCode ADC_SetRangeAll(TBoardIndex b,TUInt32 n,TPRangeCode ranges) {
    return checked([&]{ return configure(b,[&](Config &c,const Model &m) { if(!ranges||!n||n>m.adc_count*m.channels_per_adc)return EINVAL; for(uint32_t i=0;i<m.adc_count*m.channels_per_adc;++i){uint32_t r=ranges[std::min(i,n-1)],code;if(r==rcNoChange)continue;int e=range_code(r,code);if(e)return e;c.range_codes[i]=code;} return 0; }); });
}
TWinErrCode ADC_ConfigureAcquire(TBoardIndex index,TChunkingType type,TUInt32 chunk) {
    return checked([&]() -> int {auto b=get(index);if(!b)return ENODEV;std::lock_guard<std::mutex> lock(b->control);if(b->closed)return ENODEV;if(b->running())return EBUSY;uint64_t rows;if(type==ctAuto){if(chunk)return EINVAL;rows=1024;}else if(type==ctSize){if(!chunk)return EINVAL;rows=(static_cast<uint64_t>(chunk)+b->hw->model().raw_words_per_row-1)/b->hw->model().raw_words_per_row;}else return ENOTSUP;if(rows>b->hw->model().max_fifo_rows)return EINVAL;b->rows=static_cast<uint32_t>(rows);return 0;});
}
TWinErrCode ADC_AcquireBlock(TBoardIndex b,TUNative n) {return checked([&]{return n?begin(b,n):EINVAL;});}
TWinErrCode ADC_AcquireStream(TBoardIndex b) {return checked([&]{return begin(b,0);});}
TWinErrCode ADC_AcquireEnd(TBoardIndex index) {return checked([&]() -> int {auto b=get(index);if(!b)return ENODEV;std::lock_guard<std::mutex> lock(b->control);return b->stop();});}
TWinErrCode ADC_GetAcquisitionStatus(TBoardIndex index,TADCAcquisitionStatus *out) {
    return checked([&]() -> int {if(!out||out->StructSize<sizeof(*out))return EINVAL;auto b=get(index);if(!b)return ENODEV;std::lock_guard<std::mutex> lock(b->data);*out=b->status;out->QueuedSamples=b->count;if(!b->worker_done)out->ElapsedNanoseconds=ns_since(b->started);return 0;});
}
TWinErrCode ADC_WaitData(TBoardIndex index,TUInt32 ms) {
    return checked([&]() -> int {auto b=get(index);if(!b)return ENODEV;std::unique_lock<std::mutex> lock(b->data);if(!b->changed.wait_for(lock,std::chrono::milliseconds(ms),[&]{return b->count||b->worker_done;}))return ETIMEDOUT;return b->count?0:b->empty_result();});
}
TWinErrCode ADC_PollData(TBoardIndex index) {return checked([&]() -> int {auto b=get(index);if(!b)return ENODEV;std::lock_guard<std::mutex> lock(b->data);return b->count?0:b->empty_result();});}
TWinErrCode ADC_ClearData(TBoardIndex index) {return checked([&]() -> int {auto b=get(index);if(!b)return ENODEV;std::lock_guard<std::mutex> lock(b->control);std::lock_guard<std::mutex> qlock(b->data);if(!b->worker_done)return EBUSY;b->head=b->count=0;return 0;});}
TWinErrCode ADC_GetDataRaw(TBoardIndex b,TUInt32 n,TPUInt32 got,TPUInt32 raw) {return checked([&]{return read_data(b,n,got,raw,nullptr,nullptr,nullptr,nullptr);});}
TWinErrCode ADC_GetDataCounts(TBoardIndex b,TUInt32 n,TPUInt32 got,TPUInt16 counts,TPUInt16 status,TPUInt16 ts) {return checked([&]{return read_data(b,n,got,nullptr,counts,nullptr,status,ts);});}
TWinErrCode ADC_GetDataV(TBoardIndex b,TUInt32 n,TPUInt32 got,TPDouble volts,TPUInt16 status,TPUInt16 ts) {return checked([&]{return read_data(b,n,got,nullptr,nullptr,volts,status,ts);});}

// Deliberately explicit unsupported operations in this acquisition milestone.
TWinErrCode BRD_Reset(TBoardIndex) {return ENOTSUP;}
TWinErrCode BRD_GetSerialNumber(TBoardIndex,TPSerialNumber p) {if(p)*p=0;return ENOTSUP;}
TWinErrCode DAC_SetRange1(TBoardIndex,TChannelIndex,TRangeCode){return ENOTSUP;}
TWinErrCode DAC_SetRangeAll(TBoardIndex,TUInt32,TPRangeCode){return ENOTSUP;}
TWinErrCode DAC_OutputV(TBoardIndex,TChannelIndex,TDouble){return ENOTSUP;}
TWinErrCode DAC_OutputCounts(TBoardIndex,TChannelIndex,TUInt64){return ENOTSUP;}
TWinErrCode ADC_GetImmediateScanV(TBoardIndex,TPDouble,TRangeCode){return ENOTSUP;}
TWinErrCode ADC_GetImmediateScanRaw(TBoardIndex,TPUInt32,TRangeCode){return ENOTSUP;}
TWinErrCode ADC_GetImmediateV(TBoardIndex,TPDouble,TChannelIndex,TRangeCode){return ENOTSUP;}
TWinErrCode ADC_GetImmediateRaw(TBoardIndex,TPUInt32,TChannelIndex,TRangeCode){return ENOTSUP;}
TWinErrCode ADC_SetCallback(TBoardIndex,TCallbackType,TPointer){return ENOTSUP;}
TUInt8 PIO_In8(TBoardIndex,TUInt32){errno=ENOTSUP;return 0;}
TUInt16 PIO_In16(TBoardIndex,TUInt32){errno=ENOTSUP;return 0;}
TUInt32 PIO_In32(TBoardIndex,TUInt32){errno=ENOTSUP;return 0;}
TWinErrCode PIO_Out8(TBoardIndex,TUInt32,TUInt8){return ENOTSUP;}
TWinErrCode PIO_Out16(TBoardIndex,TUInt32,TUInt16){return ENOTSUP;}
TWinErrCode PIO_Out32(TBoardIndex,TUInt32,TUInt32){return ENOTSUP;}
TWinErrCode PIO_INS8(TBoardIndex,TUInt32,TUInt32,TPUInt8){return ENOTSUP;}
TWinErrCode PIO_INS16(TBoardIndex,TUInt32,TUInt32,TPUInt16){return ENOTSUP;}
TWinErrCode PIO_INS32(TBoardIndex,TUInt32,TUInt32,TPUInt32){return ENOTSUP;}
TWinErrCode PIO_OUTS8(TBoardIndex,TUInt32,TUInt32,TPUInt8){return ENOTSUP;}
TWinErrCode PIO_OUTS16(TBoardIndex,TUInt32,TUInt32,TPUInt16){return ENOTSUP;}
TWinErrCode PIO_OUTS32(TBoardIndex,TUInt32,TUInt32,TPUInt32){return ENOTSUP;}
TWinErrCode PIO_WaitForIRQ(TBoardIndex,TIRQIndex){return ENOTSUP;}
TWinErrCode PIO_PollForIRQ(TBoardIndex,TIRQIndex){return ENOTSUP;}
TWinErrCode PIO_AbortIRQWait(TBoardIndex,TIRQIndex){return ENOTSUP;}
}
