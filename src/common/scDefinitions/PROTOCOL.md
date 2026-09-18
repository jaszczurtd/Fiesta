# Serial Configurator protocol

The Fiesta Serial Configurator talks to ECU, Clocks, OilAndSpeed, and
RTC_Clock over each module's USB CDC port. One text protocol carries module
discovery, parameter reads and writes, authentication, and the request to
enter the bootloader before flashing. Debug log lines share the same stream
and never get mistaken for replies.

Adjustometer does not speak this protocol. The host skips its USB device
during discovery.

Every `SC_*` literal lives in [`sc_protocol.h`](sc_protocol.h). This file
explains how the pieces fit; the headers and the code are the reference for
exact names and values.

## Frames

Each protocol line uses one envelope:

```text
$SC,<seq>,<inner>*<crc8>\n
```

- `$SC,` marks a protocol line. The firmware ignores other lines, and the
  host counts them as `non_sc` and drops them. This is what lets `deb()` and
  `derr()` output share the stream.
- `<seq>` is a 16-bit sequence number chosen by the host. The firmware echoes
  it, so the host can pair replies with requests and skip a late reply to an
  earlier request (`wrong_seq`).
- `<inner>` is the command or reply, for example `SC_GET_META`. It must not
  contain `*`, `\r`, or `\n`.
- `<crc8>` covers the bytes between `$` and `*`. The firmware drops a frame
  with a bad CRC without replying.

Firmware and host compile the same codec from JaszczurHAL
`src/hal/serial/hal_serial_frame.h`. The host finds the JaszczurHAL checkout
through `SC_JASZCZURHAL_DIR`.

## Commands

| Group | Commands | Needs authentication |
|---|---|---|
| Session | `HELLO`, `SC_BYE` | no |
| Reads | `SC_GET_META`, `SC_GET_PARAM_LIST`, `SC_GET_VALUES`, `SC_GET_PARAM`, `SC_GET_GPS` (ECU only) | no |
| Authentication | `SC_AUTH_BEGIN`, `SC_AUTH_PROVE` | no |
| Parameter writes | `SC_SET_PARAM`, `SC_COMMIT_PARAMS`, `SC_REVERT_PARAMS` | yes |
| Flashing | `SC_REBOOT_BOOTLOADER` | yes |

`HELLO` returns the module name, firmware version, build id, and the device
UID. The UID comes from `hal_get_device_uid_hex()`; the other values are
compiled in from `SC_MODULE_TOKEN_*`, `FW_VERSION`, and `BUILD_ID`.

`SC_AUTH_BEGIN` issues a challenge and `SC_AUTH_PROVE` answers it with
HMAC-SHA256 under a key derived from the device UID. A challenge works once,
and a new `HELLO` drops authentication. The firmware counts failed proofs in
`auth_failures` but does not lock anything out yet.

## Session states

| State | Entered by | Accepted |
|---|---|---|
| inactive | start-up, `SC_BYE`, timeout, disconnect | `HELLO` and `SC_BYE` only; other commands get `SC_NOT_READY HELLO_REQUIRED` |
| active | `HELLO` | reads and authentication |
| authenticated | `SC_AUTH_PROVE` with the right answer | parameter writes and `SC_REBOOT_BOOTLOADER` |

`SC_BYE` answers `SC_OK BYE`, ends the session, and clears any pending
challenge. It works without `HAL_ENABLE_CRYPTO`, so every build can close a
session. The GUI sends it to every detected module when it disconnects;
older firmware answers `ERR UNKNOWN`, and the host logs a warning.

While a session is active, each module's `configSessionTick()` mutes the
asynchronous debug log through `hal_debug_set_muted()`. The log comes back as
soon as the session ends.

The log mute alone does not stop both RP2040 cores from writing at once.
JaszczurHAL takes one transmit mutex around every `hal_serial_print` and
`hal_serial_println`. On RP it also flushes the CDC buffer before releasing the
mutex, so a frame leaves the device before the next writer starts.

## Firmware command path

```text
USB CDC frame
  -> hal_serial_session
  -> hal_serial_commands
  -> hal_command_router
  -> sc_command_service
```

`hal_serial_session` checks the frame and handles `HELLO`, `SC_BYE`,
`SC_AUTH_BEGIN`, and `SC_AUTH_PROVE`. `hal_serial_commands` splits the
command name from its arguments and attaches the session and authentication
state. The router looks the command up by exact name and checks its source and
authentication rules. The Fiesta service in
[`sc_command_handlers.c`](sc_command_handlers.c) answers the rest.

`SC_REBOOT_BOOTLOADER` waits until its reply has left the device and only then
enters the bootloader. Modules register their commands for
`HAL_COMMAND_SOURCE_SERIAL_SESSION` only. Allowing the reboot from BLE or LoRa
would first need a way to know that the reply over that link has finished
sending.

## Parameters

Each module lists its parameters as descriptors in `config.{c,cpp}`. The
shared code in [`sc_param_handlers.c`](sc_param_handlers.c) looks them up,
checks ranges, builds the `PARAM_LIST`, `PARAM_VALUES`, and `PARAM` replies,
and packs persisted values into a versioned blob with a CRC32 (PKZIP).

Writes are staged and need an authenticated session. `SC_SET_PARAM` changes
the staged copy, `SC_COMMIT_PARAMS` validates and applies it, and
`SC_REVERT_PARAMS` copies the active values back. ECU and RTC_Clock have writable parameters; Clocks and
OilAndSpeed only report theirs.

To add a parameter, add one descriptor row and one field to the module's
values struct. If the value is persisted, raise `schema_since` as well.

| File | Contents |
|---|---|
| [`sc_protocol.h`](sc_protocol.h) | command names, status codes, reply tags, and reply formats; no HAL dependency |
| [`sc_session_vocabulary.h`](sc_session_vocabulary.h) | `fiesta_default_vocabulary`, which gives the JaszczurHAL session Fiesta's command names |
| [`sc_param_types.h`](sc_param_types.h) | `sc_param_descriptor_t`, the `READ_ONLY` and `NOT_PERSISTED` flags, and the `SC_PARAM_SCALAR_I16(...)` macros |
| [`sc_param_handlers.h`](sc_param_handlers.h) | descriptor lookup, range checks, replies, and blob encoding |
| [`sc_command_handlers.h`](sc_command_handlers.h) | command registration, argument parsing, source and authentication rules, and the deferred bootloader entry |

## Device identity

Each module shows up on USB as its own device, so the host can tell modules
apart even with several boards connected:

```text
/dev/serial/by-id/usb-Jaszczur_Fiesta_ECU_DE62A875579C612A-if00
/dev/serial/by-id/usb-Jaszczur_Fiesta_Clocks_E6625887D3475937-if00
```

| USB field | Value | Source |
|---|---|---|
| `iManufacturer` | `Jaszczur` | `identity.usbManufacturer` in `.vscode/jaszczurhal.project.json` |
| `iProduct` | `Fiesta <Module>` | `identity.usbProduct` in the same file |
| `iSerialNumber` | 16 hex digits of the flash unique id | read at run time by the JaszczurHAL RP USB code |

The same UID appears in the `HELLO` reply. The host discovers modules through
the `usb-Jaszczur_Fiesta_*` entries. After flashing, it waits for the entry with
the same UID to come back before it sends the post-flash `HELLO`.

## Flashing

Every firmware build writes `.build/firmware.uf2` and
`.build/firmware.manifest.json`. The manifest holds `module_name`,
`fw_version`, `build_id`, the SHA-256 of the UF2, and `uf2_file`, the UF2 name
without a path. The build fails when the checksum does not match.

The host rejects a manifest with a missing field, a path in `uf2_file`, or a
checksum that does not match the UF2. It checks the checksum again right
before the reboot, so choosing a manifest is enough to pick the image. The
optional `signature` field is read but not verified; ed25519 verification is
not implemented.

The configurator's flash flow then authenticates, sends
`SC_REBOOT_BOOTLOADER`, waits for the BOOTSEL drive, copies the UF2, waits for
the module to come back, and checks its `HELLO`. The steps are listed in the
[Serial Configurator README](../../SerialConfigurator/README.md#current-functionality).

The VS Code upload task goes through JaszczurHAL `jh-vscode upload`, which
checks the port against the module's USB identity before it flashes. The rules
are in the JaszczurHAL `vscode/README.md`.

## Host behaviour on a noisy link

- **Bad frames.** When a byte drop eats the leading `$`, the host restores it,
  checks CRC and sequence number as usual, and counts the frame as `repaired`.
  The first corrupt frame shortens the wait to 60 ms instead of the full
  `SC_TRANSPORT_PRIMARY_TIMEOUT_MS` (400 ms). The firmware never resends on its
  own, so the host reopens the port and retries.
- **Late replies.** Frames with an earlier sequence number count as
  `wrong_seq` and are skipped.
- **Log lines.** Lines without `$SC,` count as `non_sc`. They never reach the
  reply parser.

A timeout reports the byte, line, `non_sc`, bad-frame, `wrong_seq`,
`repaired`, and overflow counters, along with the first unexpected lines
(cut to 80 characters). Define `SC_DEBUG_DEEP` in
[`config.h`](../../SerialConfigurator/src/config.h) to log every frame and
every drive scan to stderr.
