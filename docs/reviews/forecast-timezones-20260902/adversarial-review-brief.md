Intent: Separate device-local update time from spot-local forecast times. Persist the device timezone in installer configuration version 4, migrate older stored versions safely, and invalidate cached web forecasts that carry the old display semantics.

Risk division 1 - Web time rendering and cache invalidation
Reason: A timezone can be applied to the wrong timestamp, or stale normalized data can keep the old label.
Paths: web/src/timezone.js, web/src/forecast/, web/src/config/, web/src/views/ConfiguratorView.vue

Risk division 2 - Installer contract and firmware persistence migration
Reason: Version 4 must agree across JSON schema, web digest generation, firmware validation, and v2/v3 migration.
Paths: contracts/windscout-config.schema.json, firmware/main/installed_configuration.*, firmware/main/wind_installer_service.c

Risk division 3 - Firmware display boundary
Reason: The device timezone belongs only to the retrieval timestamp; forecast dates and hourly values must continue using the spot timezone.
Paths: firmware/main/wind_app.c, firmware/main/wind_spots.*

Cross-division interaction: Verify web-created version-4 payloads are accepted by firmware with the same digest, and that older stored records remain loadable without reinstallation.
