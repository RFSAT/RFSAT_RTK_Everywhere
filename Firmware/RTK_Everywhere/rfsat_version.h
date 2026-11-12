// rfsat_version.h - RFSAT release of this firmware, shown on the boot screen, in the main menu and in
// SPGET,espFirmwareVersion. The RFSAT build workflow (.github/workflows/rfsat-build.yml) rewrites this file before
// compiling: RFSAT_RELEASE from the release tag (rfsat-v3.4-r3 -> "r3"), "dev" for other builds; RFSAT_COMMIT is the
// short commit. Kept as "dev" in the repository.
#pragma once
#define RFSAT_RELEASE "dev"
#define RFSAT_COMMIT ""
