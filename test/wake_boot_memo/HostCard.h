#pragma once
#include <SdCardFontRegistry.h>

#include <vector>

// Families the next registry walk finds, and how many walks ran.
extern std::vector<SdCardFontFamilyInfo> hostCatalog;
extern int hostDiscoveries;
extern uint8_t hostFamilyWeights;  // physical weight bits every host family reports (bit 0 the base file)
