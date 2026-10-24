#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "xr_3da/KichR4Telemetry.h"
static unsigned formatted=0,emitted=0;
static KichR4TelemetryRecord received={};
static int CountedFormat(char* dst,size_t bytes,const char* format,...){
    ++formatted;va_list args;va_start(args,format);const int result=vsnprintf(dst,bytes,format,args);va_end(args);return result;
}
static void Sink(const KichR4TelemetryRecord* record){++emitted;received=*record;}
static void ThrowingSink(const KichR4TelemetryRecord*){throw 1;}
#define snprintf CountedFormat
struct Device {
    KichR4TelemetryContext telemetry;
    KichR4TelemetrySink telemetry_sink;
    uint64_t callback_failures;
    Device():telemetry(),telemetry_sink(NULL),callback_failures(0){}
