/*------------------------------------------------------------------------------
NtripFailover.ino  (RFSAT addition)

  Ordered NTRIP correction sources with automatic failover and fail-back.

  Source 0 is the primary caster (ntripClient_* settings). Sources 1-3 are the
  alternates (ntripClientAltN_* settings). A source is usable when its host,
  port and mount point are set.

  Policy:
    * Soft failure (cannot connect, no response, banned/sandbox, connection
      broken, no RTCM for NTRIP_CLIENT_RECEIVE_DATA_TIMEOUT): move to the next
      usable source and reconnect after NTRIP_FAILOVER_RETRY_MSEC.
    * Hard failure (401 unauthorized, SOURCETABLE = unknown mount point, other
      caster error): disable that source until its settings change or reboot,
      then move on. When every source is disabled, the client shuts down as the
      original firmware did.
    * When every usable source has failed once in a row, the normal NTRIP client
      back-off (15 s, 30 s, 1 min, 2 min, 5 min ...) is applied before the next
      cycle, so casters are never hammered.
    * Fail-back: while running on a lower priority source, the client returns
      to the highest priority usable source every ntripClientFailbackInterval_s
      seconds (0 disables). If it fails, failover brings the lower priority
      source back within a few seconds.

  Typical RFSAT order:
    0: own base (EOS Arrow Gold / DJI D-RTK 2) via its caster, < 2-4 km
    1: Metrica VRS (MSM_VRS / i-MAX), GGA required
    2: Metrica nearest station (MSM_NEAR)
    3: EUREF / other nearest station
    No internet at all: the LG290P falls back to Galileo HAS (pppMode) on its own.
------------------------------------------------------------------------------*/

// NTRIP_FAILOVER_RETRY_MSEC is defined in settings.h

static NTRIP_SOURCE ntripSource;              // References for the active source
static uint8_t ntripSourceIndex = 0;          // Active source, 0 = primary
static bool ntripSourceInitialized = false;
static uint8_t ntripSourceDisabledMask = 0;   // Sources disabled by hard failures
static uint8_t ntripSourceCycleFailures = 0;  // Consecutive failures since the last good connection
static uint32_t ntripSourceConnectedMsec = 0; // When the active source connected
uint32_t ntripSourceSwitchCount = 0;          // Diagnostics: number of failovers since boot

//----------------------------------------
// Fill a reference structure for source i
//----------------------------------------
void ntripSourceGetRef(uint8_t i, NTRIP_SOURCE *src)
{
    switch (i)
    {
    default:
    case 0:
        src->host = settings.ntripClient_CasterHost;
        src->port = &settings.ntripClient_CasterPort;
        src->user = settings.ntripClient_CasterUser;
        src->userPw = settings.ntripClient_CasterUserPW;
        src->mount = settings.ntripClient_MountPoint;
        break;
    case 1:
        src->host = settings.ntripClientAlt1_CasterHost;
        src->port = &settings.ntripClientAlt1_CasterPort;
        src->user = settings.ntripClientAlt1_CasterUser;
        src->userPw = settings.ntripClientAlt1_CasterUserPW;
        src->mount = settings.ntripClientAlt1_MountPoint;
        break;
    case 2:
        src->host = settings.ntripClientAlt2_CasterHost;
        src->port = &settings.ntripClientAlt2_CasterPort;
        src->user = settings.ntripClientAlt2_CasterUser;
        src->userPw = settings.ntripClientAlt2_CasterUserPW;
        src->mount = settings.ntripClientAlt2_MountPoint;
        break;
    case 3:
        src->host = settings.ntripClientAlt3_CasterHost;
        src->port = &settings.ntripClientAlt3_CasterPort;
        src->user = settings.ntripClientAlt3_CasterUser;
        src->userPw = settings.ntripClientAlt3_CasterUserPW;
        src->mount = settings.ntripClientAlt3_MountPoint;
        break;
    }
}

//----------------------------------------
// Determine if source i has the minimum parameters
//----------------------------------------
bool ntripSourceValid(uint8_t i)
{
    NTRIP_SOURCE src;
    if (i >= NTRIP_CLIENT_SOURCES)
        return false;
    // Alternates are only used when failover is enabled
    if ((i > 0) && (settings.ntripClientFailover == false))
        return false;
    ntripSourceGetRef(i, &src);
    return (src.host[0] != 0) && (*src.port != 0) && (src.mount[0] != 0);
}

//----------------------------------------
// Determine if source i may be used now
//----------------------------------------
bool ntripSourceUsable(uint8_t i)
{
    return ntripSourceValid(i) && ((ntripSourceDisabledMask & (1 << i)) == 0);
}

//----------------------------------------
// Count the usable sources
//----------------------------------------
uint8_t ntripSourceUsableCount()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < NTRIP_CLIENT_SOURCES; i++)
        if (ntripSourceUsable(i))
            count++;
    return count;
}

//----------------------------------------
// Count the configured sources (ignores hard failures)
//----------------------------------------
uint8_t ntripSourceValidCount()
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < NTRIP_CLIENT_SOURCES; i++)
        if (ntripSourceValid(i))
            count++;
    return count;
}

//----------------------------------------
// Highest priority usable source, or the primary if none
//----------------------------------------
uint8_t ntripSourceFirstUsable()
{
    for (uint8_t i = 0; i < NTRIP_CLIENT_SOURCES; i++)
        if (ntripSourceUsable(i))
            return i;
    return 0;
}

//----------------------------------------
// Make source i the active source
//----------------------------------------
void ntripSourceSelect(uint8_t i)
{
    ntripSourceIndex = i % NTRIP_CLIENT_SOURCES;
    ntripSourceGetRef(ntripSourceIndex, &ntripSource);
    ntripSourceInitialized = true;
}

//----------------------------------------
// Get the active source, selecting the highest priority one on first use
//----------------------------------------
NTRIP_SOURCE *ntripSrc()
{
    if (!ntripSourceInitialized)
        ntripSourceSelect(ntripSourceFirstUsable());
    return &ntripSource;
}

//----------------------------------------
// Index of the active source
//----------------------------------------
uint8_t ntripSourceActiveIndex()
{
    ntripSrc();
    return ntripSourceIndex;
}

//----------------------------------------
// Return to the highest priority usable source
//----------------------------------------
void ntripSourceReset(bool clearDisabled)
{
    if (clearDisabled)
        ntripSourceDisabledMask = 0;
    ntripSourceCycleFailures = 0;
    ntripSourceSelect(ntripSourceFirstUsable());
}

//----------------------------------------
// The active source connected and the caster accepted the request
//----------------------------------------
void ntripSourceConnected()
{
    ntripSourceCycleFailures = 0;
    ntripSourceConnectedMsec = millis();
    if (ntripSourceValidCount() > 1)
        systemPrintf("NTRIP Client using correction source %d of %d: %s:%d/%s\r\n", ntripSourceIndex + 1,
                     ntripSourceValidCount(), ntripSource.host, *ntripSource.port, ntripSource.mount);
}

//----------------------------------------
// The active source failed. Select the next usable source.
//----------------------------------------
NTRIP_FAILOVER_RESULT ntripSourceFailover(bool hardFailure, const char *reason)
{
    uint8_t failed = ntripSourceActiveIndex();

    // Hard failures disable the source until its settings change
    if (hardFailure)
        ntripSourceDisabledMask |= (1 << failed);

    // Single source or failover disabled: keep the original behaviour
    if ((settings.ntripClientFailover == false) || (ntripSourceValidCount() <= 1))
        return NTRIP_FAILOVER_NONE;

    // Every source is disabled: nothing left to try
    if (ntripSourceUsableCount() == 0)
    {
        systemPrintln("NTRIP Client: all correction sources rejected the credentials or mount point");
        return NTRIP_FAILOVER_NONE;
    }

    // Find the next usable source after the failed one (wrapping around)
    uint8_t next = failed;
    for (uint8_t n = 1; n <= NTRIP_CLIENT_SOURCES; n++)
    {
        uint8_t i = (failed + n) % NTRIP_CLIENT_SOURCES;
        if (ntripSourceUsable(i))
        {
            next = i;
            break;
        }
    }

    ntripSourceCycleFailures++;
    ntripSourceSwitchCount++;
    ntripSourceSelect(next);
    systemPrintf("NTRIP Client failover (%s): source %d -> source %d %s:%d/%s\r\n", reason ? reason : "failure",
                 failed + 1, next + 1, ntripSource.host, *ntripSource.port, ntripSource.mount);

    // After every usable source has failed in a row, back off before the next cycle
    if (ntripSourceCycleFailures >= ntripSourceUsableCount())
    {
        ntripSourceCycleFailures = 0;
        // Start the next cycle from the highest priority source
        ntripSourceSelect(ntripSourceFirstUsable());
        return NTRIP_FAILOVER_BACKOFF;
    }
    return NTRIP_FAILOVER_SWITCHED;
}

//----------------------------------------
// Determine if it is time to return to a higher priority source
//----------------------------------------
bool ntripSourceFailbackDue()
{
    if ((settings.ntripClientFailover == false) || (settings.ntripClientFailbackInterval_s == 0))
        return false;
    uint8_t best = ntripSourceFirstUsable();
    if (ntripSourceActiveIndex() <= best)
        return false;
    return ((millis() - ntripSourceConnectedMsec) >= (settings.ntripClientFailbackInterval_s * 1000UL));
}

//----------------------------------------
// Print the source table (status / debug)
//----------------------------------------
void ntripSourcePrintStatus()
{
    if (ntripSourceValidCount() <= 1)
        return;
    systemPrintf("NTRIP Client sources (failover %s, fail-back %ds, switches %d):\r\n",
                 settings.ntripClientFailover ? "on" : "off", settings.ntripClientFailbackInterval_s,
                 ntripSourceSwitchCount);
    for (uint8_t i = 0; i < NTRIP_CLIENT_SOURCES; i++)
    {
        NTRIP_SOURCE src;
        if (!ntripSourceValid(i))
            continue;
        ntripSourceGetRef(i, &src);
        systemPrintf("  %c %d: %s:%d/%s%s\r\n", (i == ntripSourceIndex) ? '*' : ' ', i + 1, src.host, *src.port,
                     src.mount, (ntripSourceDisabledMask & (1 << i)) ? " (disabled: rejected)" : "");
    }
}
