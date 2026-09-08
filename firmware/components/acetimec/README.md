# AceTimeC

This directory vendors AceTimeC v0.15.0 from the upstream archive with SHA-256
`081a6b00af39ec8c5f6cfc3cf1ecea80085c004c57c669d70d886635c61c581c`.
It provides the complete IANA TZDB 2025b zone and link registry used by Windpeek.
AceTimeC is MIT licensed; see `LICENSE`.

`firmware/main/wind_timezone.c` adds forward-only overlays for the Alberta,
British Columbia, and Morocco civil-time changes published in IANA TZDB
2026b/2026c after this upstream release.
