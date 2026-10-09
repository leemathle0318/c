/*
 * VCam LIVE Audio Diagnostics v0.2 — PASSIVE, TikTok app-process only
 *
 * No audio buffers are modified. No audio is recorded or transmitted.
 * This is NOT an OBS-to-TikTok-LIVE audio fix.
 *
 * Intended for iOS 15-16 rootless; generic arm64 app process on A11/A13.
 * A separate system-process tweak would require architecture-specific work.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

typedef int32_t OSStatus;
typedef uint32_t UInt32;
typedef void *AudioUnit;
typedef void *AudioComponent;
typedef void *AudioComponentInstance;

typedef struct AudioComponentDescription {
    UInt32 componentType;
    UInt32 componentSubType;
    UInt32 componentManufacturer;
    UInt32 componentFlags;
    UInt32 componentFlagsMask;
} AudioComponentDescription;

typedef struct AudioBuffer {
    UInt32 mNumberChannels;
    UInt32 mDataByteSize;
    void *mData;
} AudioBuffer;
typedef struct AudioBufferList {
    UInt32 mNumberBuffers;
    AudioBuffer mBuffers[1];
} AudioBufferList;

typedef OSStatus (*AudioUnitRenderFn)(AudioUnit, UInt32 *, const void *,
                                       UInt32, UInt32, AudioBufferList *);
typedef OSStatus (*AudioOutputUnitStartFn)(AudioUnit);
typedef OSStatus (*AudioUnitSetPropertyFn)(AudioUnit, UInt32, UInt32,
                                            UInt32, const void *, UInt32);
typedef OSStatus (*AudioComponentInstanceNewFn)(AudioComponent,
                                                  AudioComponentInstance *);
typedef OSStatus (*AudioComponentGetDescriptionFn)(AudioComponent,
                                                    AudioComponentDescription *);
typedef void (*MSHookFunctionFn)(void *, void *, void **);

static AudioUnitRenderFn originalRender;
static AudioOutputUnitStartFn originalStart;
static AudioUnitSetPropertyFn originalSetProperty;
static AudioComponentInstanceNewFn originalNew;
static AudioComponentGetDescriptionFn getDescription;

static atomic_ullong callsRender = ATOMIC_VAR_INIT(0);
static atomic_ullong callsBus0 = ATOMIC_VAR_INIT(0);
static atomic_ullong callsBus1 = ATOMIC_VAR_INIT(0);
static atomic_ullong callsOther = ATOMIC_VAR_INIT(0);
static atomic_ullong callsRenderError = ATOMIC_VAR_INIT(0);
static atomic_ullong callsRenderData = ATOMIC_VAR_INIT(0);
static atomic_ullong framesBus1 = ATOMIC_VAR_INIT(0);
static atomic_ullong callsStart = ATOMIC_VAR_INIT(0);
static atomic_ullong callsSetProperty = ATOMIC_VAR_INIT(0);
static atomic_ullong setBus1FourByte = ATOMIC_VAR_INIT(0);
static atomic_ullong callsNew = ATOMIC_VAR_INIT(0);
static atomic_ullong newRemoteIO = ATOMIC_VAR_INIT(0);
static atomic_ullong newVoiceProcessingIO = ATOMIC_VAR_INIT(0);
static atomic_ullong newOther = ATOMIC_VAR_INIT(0);
static atomic_uint lastProperty = ATOMIC_VAR_INIT(0);
static char logPath[PATH_MAX];
static char hookStatus[280];

#define FOURCC(a,b,c,d) (((UInt32)(a)<<24)|((UInt32)(b)<<16)|((UInt32)(c)<<8)|((UInt32)(d)))

static OSStatus hookRender(AudioUnit au, UInt32 *flags, const void *timestamp,
                           UInt32 bus, UInt32 frames, AudioBufferList *data) {
    if (!originalRender) return -1;
    OSStatus result = originalRender(au, flags, timestamp, bus, frames, data);
    atomic_fetch_add_explicit(&callsRender, 1, memory_order_relaxed);
    if (bus == 0) atomic_fetch_add_explicit(&callsBus0, 1, memory_order_relaxed);
    else if (bus == 1) {
        atomic_fetch_add_explicit(&callsBus1, 1, memory_order_relaxed);
        if (result == 0) atomic_fetch_add_explicit(&framesBus1, frames, memory_order_relaxed);
    } else atomic_fetch_add_explicit(&callsOther, 1, memory_order_relaxed);
    if (result != 0) atomic_fetch_add_explicit(&callsRenderError, 1, memory_order_relaxed);
    if (result == 0 && data && data->mNumberBuffers > 0 &&
        data->mNumberBuffers <= 8 && data->mBuffers[0].mData &&
        data->mBuffers[0].mDataByteSize != 0) {
        atomic_fetch_add_explicit(&callsRenderData, 1, memory_order_relaxed);
    }
    /* PASSIVE: do not touch PCM. No filesystem I/O in this real-time hook. */
    return result;
}

static OSStatus hookStart(AudioUnit au) {
    atomic_fetch_add_explicit(&callsStart, 1, memory_order_relaxed);
    return originalStart ? originalStart(au) : -1;
}

static OSStatus hookSetProperty(AudioUnit au, UInt32 prop, UInt32 scope,
                                 UInt32 element, const void *value, UInt32 len) {
    atomic_fetch_add_explicit(&callsSetProperty, 1, memory_order_relaxed);
    atomic_store_explicit(&lastProperty, prop, memory_order_relaxed);
    /* Observational hint only: this does not prove microphone input is on. */
    if (element == 1 && len == sizeof(UInt32) && value) {
        atomic_fetch_add_explicit(&setBus1FourByte, 1, memory_order_relaxed);
    }
    return originalSetProperty ? originalSetProperty(au, prop, scope, element, value, len) : -1;
}

static OSStatus hookNew(AudioComponent component, AudioComponentInstance *instance) {
    atomic_fetch_add_explicit(&callsNew, 1, memory_order_relaxed);
    if (getDescription && component) {
        AudioComponentDescription desc = {0};
        if (getDescription(component, &desc) == 0) {
            if (desc.componentSubType == FOURCC('r','i','o','c'))
                atomic_fetch_add_explicit(&newRemoteIO, 1, memory_order_relaxed);
            else if (desc.componentSubType == FOURCC('v','p','i','o'))
                atomic_fetch_add_explicit(&newVoiceProcessingIO, 1, memory_order_relaxed);
            else atomic_fetch_add_explicit(&newOther, 1, memory_order_relaxed);
        }
    }
    return originalNew ? originalNew(component, instance) : -1;
}

static void writeSnapshot(const char *event) {
    FILE *f = fopen(logPath, "a");
    if (!f) return;
    fprintf(f, "epoch=%lld event=%s AUStart=%llu AURender=%llu bus0=%llu bus1=%llu "
            "busOther=%llu framesBus1=%llu pcmNonempty=%llu renderErrors=%llu "
            "AUSetProperty=%llu setBus1FourByte=%llu lastProperty=%u "
            "AUCreate=%llu RemoteIO=%llu VoiceProcessingIO=%llu otherComponents=%llu\n",
            (long long)time(NULL), event,
            (unsigned long long)atomic_load(&callsStart),
            (unsigned long long)atomic_load(&callsRender),
            (unsigned long long)atomic_load(&callsBus0),
            (unsigned long long)atomic_load(&callsBus1),
            (unsigned long long)atomic_load(&callsOther),
            (unsigned long long)atomic_load(&framesBus1),
            (unsigned long long)atomic_load(&callsRenderData),
            (unsigned long long)atomic_load(&callsRenderError),
            (unsigned long long)atomic_load(&callsSetProperty),
            (unsigned long long)atomic_load(&setBus1FourByte),
            (unsigned)atomic_load(&lastProperty),
            (unsigned long long)atomic_load(&callsNew),
            (unsigned long long)atomic_load(&newRemoteIO),
            (unsigned long long)atomic_load(&newVoiceProcessingIO),
            (unsigned long long)atomic_load(&newOther));
    fclose(f);
}

static void *loggerThread(void *unused) {
    (void)unused;
    FILE *f = fopen(logPath, "a");
    if (f) {
        fprintf(f, "# VCam LIVE Audio Diagnostics v0.2: PASSIVE, does not capture audio\n# %s\n", hookStatus);
        fclose(f);
    }
    writeSnapshot("initialized");
    for (;;) {
        sleep(5);
        writeSnapshot("sample");
    }
    return NULL;
}

static int installHook(MSHookFunctionFn hooker, void *symbol, void *replacement, void **original) {
    if (!hooker || !symbol) return 0;
    hooker(symbol, replacement, original);
    return *original != NULL;
}

__attribute__((constructor)) static void probeInit(void) {
    const char *temp = getenv("TMPDIR");
    if (!temp || !temp[0]) temp = "/tmp";
    int n = snprintf(logPath, sizeof(logPath), "%s%sVCamLiveProbe.log",
                     temp, temp[strlen(temp) - 1] == '/' ? "" : "/");
    if (n < 0 || (size_t)n >= sizeof(logPath)) return;

    /* Normal framework load. This project does not bypass TikTok security. */
    void *audio = dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox", RTLD_NOW | RTLD_GLOBAL);
    MSHookFunctionFn hooker = (MSHookFunctionFn)dlsym(RTLD_DEFAULT, "MSHookFunction");
    void *(*lookup)(void *, const char *) = dlsym;
    void *render = lookup(audio ? audio : RTLD_DEFAULT, "AudioUnitRender");
    void *start = lookup(audio ? audio : RTLD_DEFAULT, "AudioOutputUnitStart");
    void *setprop = lookup(audio ? audio : RTLD_DEFAULT, "AudioUnitSetProperty");
    void *newcomponent = lookup(audio ? audio : RTLD_DEFAULT, "AudioComponentInstanceNew");
    getDescription = (AudioComponentGetDescriptionFn)lookup(audio ? audio : RTLD_DEFAULT, "AudioComponentGetDescription");
    int r = installHook(hooker, render, (void *)&hookRender, (void **)&originalRender);
    int s = installHook(hooker, start, (void *)&hookStart, (void **)&originalStart);
    int p = installHook(hooker, setprop, (void *)&hookSetProperty, (void **)&originalSetProperty);
    int c = installHook(hooker, newcomponent, (void *)&hookNew, (void **)&originalNew);
    snprintf(hookStatus, sizeof(hookStatus),
             "MSHookFunction=%s Render=%s Start=%s SetProperty=%s ComponentNew=%s",
             hooker ? "found" : "MISSING", r ? "hooked" : "NOT_HOOKED",
             s ? "hooked" : "NOT_HOOKED", p ? "hooked" : "NOT_HOOKED",
             c ? "hooked" : "NOT_HOOKED");
    pthread_t t;
    if (pthread_create(&t, NULL, loggerThread, NULL) == 0) pthread_detach(t);
}
