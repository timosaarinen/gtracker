# SID implementation status

The complete product contract is in [the implementation plan](sid-tracker-implementation-plan.md).
The current implementation is a document foundation, not a working SID tracker.

The independently testable `gtracker_sid` library supports version 1 source
documents, stable pattern/section/order IDs, explicit NOTE/OFF events and sparse
HOLD rows. It validates references, unique IDs and rows, three physical voices,
64-row patterns, source size, pitch/preset bounds, PAL/6581 timing and bank version.
Drums store canonical MIDI 60. Scientific middle C displays as C-4.

Native inspection commands after a headless build:

```sh
.local/build/headless/bin/gtracker_cli sid-validate FILE --json
.local/build/headless/bin/gtracker_cli sid-info FILE --json
```

Blank songs can be saved. Completed-tune validation requires a NOTE in an
arranged pattern. Atomic saves use the existing sibling-temporary-file writer.
Preset names/roles are reserved; no sound programs or released bank sound hash
exist yet. Revision values currently have an explicit 1..1000000 bound.

The JSON schema describes structural constraints. Native validation additionally
checks global/reference constraints and UTF-8 byte lengths. No SID export,
renderer, audition, transport, editor screen, PRG launcher or offline backend
package is implemented yet. No GUI, listening or VICE evidence is claimed.
