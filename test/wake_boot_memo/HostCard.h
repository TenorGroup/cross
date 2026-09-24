#pragma once
#include <SdCardFontRegistry.h>

#include <vector>

// Families the next registry walk finds, and how many walks ran.
extern std::vector<SdCardFontFamilyInfo> hostCatalog;
extern int hostDiscoveries;
