# Viewer guards and network scope

These safeguards address concrete input and file-writing defects. They do not identify or
promise to remove Elastic's VirusTotal detection. Local tracking statistics remain enabled.

## Network scope

The packaged viewer connects through `lh::net::ConnectLoopback`. Both the client address and
the driver's listener address are explicitly `127.0.0.1`; there is no remote host option.
Windows additionally checks that the established peer belongs to the same Windows user.
That is a user boundary, not proof that the peer is an authentic Openhouse binary.

The packaged viewer, driver, and personal installer source was checked for HTTP, DNS,
Internet client APIs, and alternative socket destinations. No outbound Internet telemetry
path was found in that reviewed source. Report export writes a local ZIP and does not upload it.
The report can contain room layout, device identifiers, recordings, and SteamVR logs.
Review its contents before sharing it.

No Openhouse or SteamVR host process was running during the local connection snapshot.
This is not a packet-capture test or a complete audit of GPU drivers, SteamVR, Quest software,
or other third-party runtime code. Those programs can independently contact remote services.
CI dependency downloads are separate from the packaged applications' runtime telemetry.

## Input guards

The viewer requires the `openhouse` protocol-1 greeting before accepting state or log messages.
Malformed JSON, incorrect collection types, and incompatible greetings disconnect the peer.
Unknown fields and unknown post-greeting message types remain compatible with additive updates.

Input reading is limited to 64 KiB per UI poll. The existing 256 KiB message limit remains.
Each telemetry collection is limited to 256 entries. Pose vectors must have the expected
length and finite values; positions and velocities are bounded to a generous magnitude of
one million, and quaternion components to two. These display limits reject overflow values,
not ordinary room-scale movement. Integer fields are checked before conversion.

Viewer settings are limited to 64 KiB. A malformed or incorrectly typed file is ignored
without partially applying its values or throwing a JSON exception out of startup.
Settings are staged and atomically replaced. A failed save leaves the previous file intact
and reports the error to stderr. An ignored malformed file can be replaced with current
defaults when the viewer saves settings on exit.

## Diagnostic report writes

The report ZIP is written to staging on the destination volume. Only a completed ZIP is
published. Publishing refuses an existing destination, including another report saved with
the same second-resolution timestamp and process ID. A failed report no longer removes that
existing path. If a name collision occurs, retry saving after the timestamp changes.

This shared ZIP writer is also used by the driver's existing local report command.
Report collection and its existing 4 GiB ZIP format limits are otherwise unchanged.

## Verification

`tests/openvr/test_viewer_guards.cpp` covers valid statistics and logs, required greetings,
malformed messages, size and numeric boundaries, safe settings loading and replacement,
and completed/failed/colliding ZIP writes. It is part of `openvr_components` in Windows CI.
The local Zig safety suite also checks that publishing a new file cannot replace an old one.

These checks do not establish improved tracking, a clean antivirus verdict, or real VR compatibility.
The previously created draft release is immutable for this change; its binary assets are not
silently replaced by a source update.
