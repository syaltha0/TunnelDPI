# Native dialog tests

Run from `app` in PowerShell with MinGW on PATH. The tests open native dialogs and navigate them automatically. Setup tests use a development build that simulates installation; they do not install files, change the registry or launch TunnelDPI.

```powershell
mingw32-make
$testOut = Join-Path $env:TEMP 'tunneldpi-ui-checks'
New-Item -ItemType Directory -Force $testOut | Out-Null
gcc '-Bres/nomanifest/' -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 tests/guide_ui.c src/i18n.c res/app.o -lcomctl32 -o "$testOut/guide-test.exe"
gcc '-Bres/nomanifest/' -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 tests/setup_ui.c res/app.o -lcomctl32 -lshell32 -lole32 -luuid -ladvapi32 -luser32 -lgdi32 -o "$testOut/setup-test.exe"
& "$testOut/guide-test.exe"
& "$testOut/setup-test.exe"
```

Both programs return zero on success. The guide test checks next, back, finish and cancel in Turkish and English. Setup checks the Wizard97 welcome, destination, startup, progress and finish pages, including back navigation, fresh install, update, simulated write failure, cancellation and the startup preference in both languages. Run `setup-test.exe --show` for an interactive preview that simulates installation. A real installation still needs a separate manual check.

Development builds compiled with `-DDEV_SHOT` also support `--qualitytest`. Run this in a separate fixture folder: it writes a test configuration and `qualitytest.txt`. It checks report privacy using a log line containing a domain, IP, user path and secret; search completion/cancellation; release-note persistence; and 64 combinations of language, page and DPI (96/120/144/192), including button bounds and keyboard focus.

The sharing status regression uses mocks and never opens a socket or changes the firewall:

```powershell
gcc -Os -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 tests/share_state.c -o "$testOut/share-state.exe"
& "$testOut/share-state.exe"
```
