# Phase 5 validation

The native build and headless regression checks are run by
`.github/workflows/native.yml` and the tag release workflow. A local Release
build can be reproduced with:

```powershell
.\build-windows.ps1
```

`scripts/verify-release.ps1` enforces the 5 MiB EXE limit, x64 PE headers,
native-only imports, and the `VERSIONINFO` fields. It also reports the
signature state. `scripts/sign-release.ps1` signs with a certificate from
`Cert:\CurrentUser\My` and verifies the Authenticode result; pass the thumbprint
to `build-windows.ps1` to run both steps.

The following checks require an interactive Windows 11 x64 desktop and are
intentionally manual:

- Move the main window and command dialog between 100%, 125%, 150%, 175%, and
  200% DPI monitors. Confirm that fonts, columns, splitter, dialog contents,
  and minimum sizes remain usable.
- Navigate every main-window and dialog control with Tab, Shift+Tab, access
  keys, Enter, Escape, Space, Home, End, Ctrl+Home, Ctrl+End, Ctrl+A, and
  Shift+Up/Down. Confirm the visible focus indicator and screen-reader names.
- Restart Explorer (`explorer.exe`) while the app is minimized to the tray.
  Confirm that the icon returns and that Open, Start All, Stop All, and Exit
  still work.
- Repeat minimize/restore and Explorer restart cycles while commands are
  running. Confirm that logs and process state survive and that no stale tray
  icon or window remains.
- Submit the signed EXE and its SHA-256 hash to the organization's antivirus
  false-positive process. Do not use UPX after signing.
