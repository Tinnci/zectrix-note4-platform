# Note4 Platform 2.0 migration

This is an explicitly source-breaking naming migration, not a compatibility
alias release. The GitHub repository URL remains unchanged. No release tag,
device flash, NVS erase, library initialization or Android uninstall is performed
by this change.

## Source and tools

| Previously | Now |
| --- | --- |
| `zectrix::sdk::v1`, `zectrix::…` | `note4::sdk::v2`, `note4::…` |
| `components/zectrix_*`, `zectrix_*.h` | `components/note4_*`, `note4_*.h` |
| `zectrix/zectrix_sdk.h` | `note4/note4_sdk.h` |
| `ZECTRIX_*`, `CONFIG_ZECTRIX_*` | `NOTE4_*`, `CONFIG_NOTE4_*` |
| `CONFIG_ZECTRIX_DEMO_*` | `CONFIG_NOTE4_QUALIFICATION_*` |
| `zectrix_epd_demo.bin/.elf/.map` | `note4_platform.bin/.elf/.map` |
| `ZectrixDemoUi`, `ZectrixCanvas` and their forwarding headers | `UiEngine`, `Canvas` and canonical UI headers |
| `zectrix>` maintenance prompt | `note4>`; use matching host tools |
| `dev.zectrix.note4.companion` Android package | `dev.note4.companion` |
| `application/vnd.zectrix.enroll.v1` NFC MIME | `application/vnd.note4.enroll.v1` |

Rebuild native applications against SDK 2.0.0; old source names and ABI symbols
are not exported. SDK status/input numeric values and the existing serialized
book, app, page and Companion/USB packet layouts are unchanged. Branding is not
a reason to change their binary format versions, BLE UUIDs or persistent keys.

Update shell environment variables to `NOTE4_*` and source
`tools/activate-dev-env.sh` again. The activator prefers a
`note4-cmake-3.30.5` installation but accepts the existing external
`zectrix-cmake-3.30.5` directory when no new path is installed. It does not move
or reinstall a user's environment.

## Preserve configuration and device data

Copy an existing developer configuration into a new build directory:

```bash
uv run --no-project tools/migrate-config.py sdkconfig build-note4/sdkconfig
source tools/activate-dev-env.sh
idf.py --ccache -B build-note4 -D SDKCONFIG="$PWD/build-note4/sdkconfig" build
uv run --no-project tools/firmware_budget.py build-note4
```

The copier preserves values and unrelated ESP-IDF options, converts only legacy
key names, rejects conflicting old/new values and refuses to overwrite an
existing output. Source `sdkconfig` remains unchanged. Use a fresh directory or
explicitly inspect an existing migrated config before regenerating it. Named
Full/Minimal/Reader builds still use their committed profiles instead of saved
developer configuration.

The on-media NVS namespace remains `zectrix` intentionally. This is a physical
storage compatibility identifier, not the product API. Settings, credentials,
bookmarks, the books partition and BLE bonds must not disappear just because
source identifiers change. Partition offsets, factory reset confirmation, OTA
validation and recovery behavior remain unchanged.

Android treats the new application ID as a separate app, not an upgrade of the
old package. Its approved devices, private queue and Keystore identity are not
automatically imported. Finish pending transfers, install the matching new
companion and perform enrollment again. Keep the old app/data until verification;
do not silently uninstall it or copy private credentials between packages.
Use the new NFC MIME with the new application; a pre-migration APK does not
handle that record type.

The HA bridge changes state topics from `zectrix/note4/<id>/state` to
`note4/<id>/state`, and discovery/device identifiers from `zectrix_note4_<id>`
to `note4_<id>`. Existing retained discovery messages are not deleted remotely.
After checking the new entities, explicitly remove the old retained config
topics and update any automations that reference old entity IDs. BTHome packet
format and sensor object IDs are unchanged; no HA credentials are migrated.

## Display and ownership

See [display architecture](DISPLAY_ARCHITECTURE.md). The SSD2683 driver still
owns hardware; the display service owns power/policy/orientation; UI owns
logical layout. Packed transforms are private, hardware-free functions. Mono
rotation now requires 15KB instead of eagerly reserving the 60KB grayscale tier.

The bounded HTTPS test capability now requests this repository's public
`version.txt` from `raw.githubusercontent.com` on both the phone and firmware.
It remains a fixed allowlisted capability, with TLS verification and redirects
disabled; it does not permit arbitrary URLs or replace the configurable edge
page service. The qualification NFC URL points to the actual GitHub repository.

## Historical and legal identifiers

Upstream URLs, hardware identifiers, licenses, copyright holders, trademark
disclaimers and historical qualification/release records are preserved. Existing
GitHub links cannot be renamed without changing the remote repository itself.
Remaining legacy names in the configuration copier, storage namespace and
installation fallback are deliberate migration boundaries, not active product
branding.
