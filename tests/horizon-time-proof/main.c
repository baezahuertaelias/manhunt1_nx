#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(__SWITCH__) && defined(__fastcall)
#undef __fastcall
#endif
#include "win32_compat.h"
#ifdef __SWITCH__
#include <switch.h>
#endif

static DWORD last_error;
DWORD GetLastError(void) { return last_error; }
VOID SetLastError(DWORD code) { last_error = code; }
/* Exact implementation included by the production Horizon compatibility unit. */
#include "win32_compat_horizon_time.inc"

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { if (failures < 20) printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
#define DAY_TICKS 864000000000ULL
static int leap(unsigned y) { return !(y%4) && ((y%100) || !(y%400)); }
static unsigned month_days(unsigned y,unsigned m)
{
    static const unsigned d[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return d[m-1]+(m==2 && leap(y));
}
static uint64_t ticks_of(FILETIME f) { return (uint64_t)f.dwHighDateTime<<32|f.dwLowDateTime; }
static FILETIME filetime(uint64_t ticks) { FILETIME f={(DWORD)ticks,(DWORD)(ticks>>32)};return f; }
static void fields_equal(const SYSTEMTIME *s,const SYSTEMTIME *wanted,unsigned weekday)
{
    CHECK(s->wYear==wanted->wYear && s->wMonth==wanted->wMonth && s->wDay==wanted->wDay);
    CHECK(s->wHour==wanted->wHour && s->wMinute==wanted->wMinute && s->wSecond==wanted->wSecond);
    CHECK(s->wMilliseconds==wanted->wMilliseconds && s->wDayOfWeek==weekday);
}
static void date_case(SYSTEMTIME s,uint64_t days)
{
    uint64_t wanted=days*DAY_TICKS+((uint64_t)s.wHour*3600+s.wMinute*60+s.wSecond)*10000000+s.wMilliseconds*10000;
    FILETIME f={0xA5A5A5A5,0xA5A5A5A5};SYSTEMTIME decoded;
    SetLastError(0x12345678);
    CHECK(SystemTimeToFileTime(&s,&f));CHECK(ticks_of(f)==wanted);CHECK(GetLastError()==0x12345678);
    memset(&decoded,0xA5,sizeof decoded);
    CHECK(FileTimeToSystemTime(&f,&decoded));fields_equal(&decoded,&s,(unsigned)((days+1)%7));
    CHECK(GetLastError()==0x12345678);
}
static void known_dates(void)
{
    const struct { SYSTEMTIME s;uint64_t ticks; } known[]={
        {{.wYear=1601,.wMonth=1,.wDay=1},0},
        {{.wYear=1969,.wMonth=12,.wDay=31},116443872000000000ULL},
        {{.wYear=1970,.wMonth=1,.wDay=1},116444736000000000ULL},
        {{.wYear=2000,.wMonth=2,.wDay=29},125962560000000000ULL},
        {{.wYear=30827,.wMonth=12,.wDay=31,.wHour=23,.wMinute=59,.wSecond=59,.wMilliseconds=999},9223149887999990000ULL}
    };
    for(unsigned i=0;i<sizeof known/sizeof known[0];i++) {
        FILETIME f;CHECK(SystemTimeToFileTime(&known[i].s,&f));CHECK(ticks_of(f)==known[i].ticks);
    }
    SYSTEMTIME ignored=known[3].s;FILETIME f;
    ignored.wDayOfWeek=65535;CHECK(SystemTimeToFileTime(&ignored,&f));CHECK(ticks_of(f)==known[3].ticks);
}
static void invalid_dates(void)
{
    SYSTEMTIME valid={.wYear=2000,.wMonth=1,.wDay=1};
    const struct { unsigned field;WORD value; } invalid[]={
        {0,0},{0,1600},{0,30828},{0,65535},{1,0},{1,13},{1,65535},
        {3,0},{3,32},{3,65535},{4,24},{4,65535},{5,60},{5,65535},
        {6,60},{6,65535},{7,1000},{7,65535}
    };
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++) {
        SYSTEMTIME bad=valid;WORD fields[8];memcpy(fields,&bad,sizeof fields);
        fields[invalid[i].field]=invalid[i].value;memcpy(&bad,fields,sizeof bad);
        FILETIME f={0x12345678,0x9ABCDEF0},before=f;SetLastError(0x12345678);
        CHECK(!SystemTimeToFileTime(&bad,&f));CHECK(!memcmp(&f,&before,sizeof f));CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
    }
    const unsigned years[]={1900,2000,2001,2100,2400};
    for(unsigned i=0;i<sizeof years/sizeof years[0];i++)for(unsigned m=1;m<=12;m++) {
        SYSTEMTIME bad=valid;bad.wYear=years[i];bad.wMonth=m;bad.wDay=month_days(years[i],m)+1;
        FILETIME f={1,2},before=f;CHECK(!SystemTimeToFileTime(&bad,&f));CHECK(!memcmp(&f,&before,sizeof f));
    }
    FILETIME f={1,2},before=f;CHECK(!SystemTimeToFileTime(NULL,&f));CHECK(!memcmp(&f,&before,sizeof f));
    CHECK(!SystemTimeToFileTime(&valid,NULL));
}
static void calendar_sweep(void)
{
    uint64_t days=0;
    /* Reference advances day by day, independently of the civil-era formula. */
    for(unsigned y=1601;y<=30827;y++)for(unsigned m=1;m<=12;m++) {
        unsigned n=month_days(y,m);
        for(unsigned d=1;d<=n;d++) {
            if(y<=2000 || (m==1&&d==1) || (m==12&&d==31)) {
                SYSTEMTIME s={.wYear=y,.wMonth=m,.wDay=d,.wDayOfWeek=65535,
                    .wHour=days%24,.wMinute=days%60,.wSecond=(days/60)%60,.wMilliseconds=(days*37)%1000};
                date_case(s,days);
            }
            days++;
        }
    }
}
static SYSTEMTIME reference_inverse(uint64_t ticks)
{
    uint64_t days=ticks/DAY_TICKS,remaining=days;unsigned y=1601,m=1;
    while(remaining>=(unsigned)(365+leap(y))) { remaining-=365+leap(y);y++; }
    while(remaining>=month_days(y,m)) { remaining-=month_days(y,m);m++; }
    uint64_t seconds=(ticks/10000000)%86400;
    SYSTEMTIME s={.wYear=y,.wMonth=m,.wDay=remaining+1,.wDayOfWeek=(days+1)%7,
        .wHour=seconds/3600,.wMinute=(seconds/60)%60,.wSecond=seconds%60,.wMilliseconds=(ticks/10000)%1000};
    return s;
}
static void inverse_cases(void)
{
    const uint64_t boundary[]={0,1,9999,10000,9999999,10000000,116444735999999999ULL,
        116444736000000000ULL,9223149887999990000ULL,INT64_MAX};
    uint64_t random=0x6381DA7C923451BFULL;
    for(unsigned i=0;i<sizeof boundary/sizeof boundary[0]+512;i++) {
        random=random*6364136223846793005ULL+1442695040888963407ULL;
        uint64_t ticks=i<sizeof boundary/sizeof boundary[0]?boundary[i]:random&INT64_MAX;
        FILETIME f=filetime(ticks);SYSTEMTIME out,expected=reference_inverse(ticks);
        CHECK(FileTimeToSystemTime(&f,&out));fields_equal(&out,&expected,expected.wDayOfWeek);
    }
    FILETIME max=filetime(INT64_MAX);SYSTEMTIME out;
    CHECK(FileTimeToSystemTime(&max,&out));
    CHECK(out.wYear==30828&&out.wMonth==9&&out.wDay==14&&out.wHour==2&&out.wMinute==48&&out.wSecond==5&&out.wMilliseconds==477);
    const uint64_t invalid[]={0x8000000000000000ULL,UINT64_MAX,0xDEADBEEF12345678ULL};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++) {
        FILETIME f=filetime(invalid[i]);SYSTEMTIME s,before;memset(&s,0xA5,sizeof s);before=s;SetLastError(0);
        CHECK(!FileTimeToSystemTime(&f,&s));CHECK(!memcmp(&s,&before,sizeof s));CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
    }
    SYSTEMTIME s,before;memset(&s,0xA5,sizeof s);before=s;
    CHECK(!FileTimeToSystemTime(NULL,&s));CHECK(!memcmp(&s,&before,sizeof s));CHECK(!FileTimeToSystemTime(&max,NULL));
}
int main(void)
{
    CHECK(sizeof(SYSTEMTIME)==8*sizeof(WORD));
    known_dates();invalid_dates();calendar_sweep();inverse_cases();
    char result[160];snprintf(result,sizeof result,"HZ-TIME-PROOF %s checks=%u failures=%u",failures?"FAIL":"PASS",checks,failures);puts(result);
#ifdef __SWITCH__
    svcOutputDebugString(result,strlen(result));
#endif
    return failures!=0;
}
