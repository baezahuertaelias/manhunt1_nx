# Horizon UTC calendar conversion

Task HZ-TIME-001. Independent source review and host1,632,300checks pass, including old-code defect reproduction and174expected removed-validation failures. Horizon1,632,300checks, fullSwitchbuild and independent25artifact review pass. No game or physical Switch acceptance follows from this work.

## Scope and observed source defects

The private Horizon `timegm` helper has one caller, `SystemTimeToFileTime`, in `src/platform/win32_compat.c`. `kernel_rtl.c` forwards Xbox time-field conversion through the compatibility functions; thunk ordinals304/305 route these APIs. Specific original-title caller addresses and actual date inputs remain UNVERIFIED.

The existing forward function always returns TRUE, validates no fields and casts signed Xbox TIME_FIELDS into unsigned WORD fields before conversion. It therefore accepts invalid leap dates, invalid field ranges and negative fields represented as65535. Invalid large years can wrap unsigned tick arithmetic. The inverse rejects every FILETIME before the Unix epoch despite the declared1601 FILETIME epoch, ignores `gmtime_r` failure and does not reject the high bit.

For valid documented SYSTEMTIME inputs, there is no confirmed arithmetic overflow in the existing civil-date helper: the maximum30827-12-31T23:59:59.999 produces9223149887999990000ticks, below INT64_MAX. Casting negative Unix seconds to unsigned before multiplying is modular arithmetic that cancels correctly for valid1601–1969 times; that cast alone is not the defect. The broader claim that the private helper must normalize arbitrary `struct tm` fields is unnecessary once its only caller validates calendar fields.

Microsoft documents the SYSTEMTIME input ranges, including years1601–30827 and ordinary clock-field limits; the date must also exist in its month. `wDayOfWeek` is ignored by forward conversion. Invalid dates can make SystemTimeToFileTime fail. [SYSTEMTIME](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-systemtime), [SystemTimeToFileTime](https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-systemtimetofiletime).

The inverse FILETIME contract accepts values below0x8000000000000000. Its largest accepted tick decodes to30828-09-14T02:48:05.477, so the inverse's full tick domain is intentionally distinct from the documented forward SYSTEMTIME input-year range. [FileTimeToSystemTime](https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-filetimetosystemtime).

## Minimal correction

Maintained patch0013 removes the Horizon-only private `timegm` and includes `win32_compat_horizon_time.inc` for the two Horizon conversions. Existing POSIX conversion bodies are retained unchanged behind the other branch. The new unit validates Gregorian date/range fields, ignores weekday on input, uses bounded integer arithmetic directly relative to1601 and decodes valid ticks without libc `time_t` or `gmtime_r` range dependencies. Submillisecond ticks are truncated, preserving the previous conversion's millisecond policy.

Failure returns FALSE, sets the compatibility layer's ERROR_INVALID_PARAMETER and preserves the output object. This error code/output-preservation policy is the project's explicit contract; the linked Microsoft pages do not specify that exact code or output contents. Success preserves the previous last-error value. Xbox wrapper failure behavior remains unchanged: its reverse wrapper zeroes its guest output on FALSE.

## Executed proof

`scripts/horizon-time-test.sh host|switch` reconstructs the pinned runtime and applies maintained patches. It retains the original source and runs a small extracted original-code control to reproduce invalid-date acceptance and pre1970 rejection. The positive test includes the exact production Horizon unit with the real platform types. A deliberate removal of field validation must fail the same checks.

The independent test reference advances calendar days/months rather than using the production civil-era formula. Cases cover every date in a400-year Gregorian cycle, first/last dates of every remaining supported year, known1601/1969/1970/2000/max-year epochs, weekday ignoring, invalid field/month/leap dates, unsigned representations of negative fields, fractional ticks/milliseconds, the full valid FILETIME boundary, deterministic randomized inverse values, output preservation and invalid high-bit ticks.

The tests provide a single-thread last-error shim; they verify this unit's calls and preservation policy, not the unchanged runtime TLS implementation. Full Switch build and independent source/artifact review pass. General clock queries, timezone behavior, Xbox date usage, save compatibility, host POSIX semantics and physical Switch behavior are outside this proof and remain UNVERIFIED where not already measured elsewhere.

Final NRO `83da2b0b22879452bf73ee2b683b40fde65a4d582c8a0ddf0772e4f96512d18f` passes1,632,300checks in Eden. Full game NRO `1f93749d7d7b534da5608ac35f77f957a3f8995ee8de4a81cc926fb44fdf5f48` builds. Independent review verified all25 frozen artifacts at `game/build/evidence/horizon-time-83da2b0b-20261029/manifest.json`. The legacy extraction required its existing fill_systemtime dependency; an initial NRO test-header calling-convention macro conflict is retained separately from the corrected strict-warning build.
