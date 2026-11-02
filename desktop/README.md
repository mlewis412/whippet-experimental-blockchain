# Whippet Desktop

A compact native C++/Qt wallet, solo miner and full-node interface. Silver and
charcoal themes follow the approved neutral design. The application manages
the official, checksum-verified Whippet Core 1.3.1 daemon in a separate data
directory. No browser engine or Python runtime is needed by end users.

## Install and test

Windows: run `Whippet-0.1.0-Windows-Setup.exe`. Installation is per user, requires
no administrator account, and creates Start menu/optional desktop shortcuts.
Uninstalling removes application files and preserves wallet/chain data.

Linux: install the `.deb` using your software installer. The native package is
built for Ubuntu 24.04 x64 and pulls in Qt through the package manager. The
`.tar.gz` additionally requires the same Qt 6 runtime libraries. It does not
overwrite system Qt libraries or the system Node.js executable.

macOS: open the `.dmg` and drag Whippet to Applications. The initial package
targets Intel Macs; Apple Silicon requires Rosetta. Native Apple Silicon node
builds are a separate release task because the pinned upstream macOS daemon
is Intel. macOS packages need a macOS build runner.

Development packages are unsigned. Public Windows signing and Apple signing/
notarization need the release owner's certificates and are not configured here.
No public release is automatically created by the build workflow.

On first launch select **Local test chain** to try mining and payments without
network access. Test coins are private to that chain. Coinbase rewards mature
after 60 blocks on regtest. Select **Experimental mainnet** in Settings to run
the experimental public network; mining waits for sync and peers there.

### Daily use

- **Mine:** start/stop solo Scrypt work, choose the payout address, view accepted
  blocks/session rewards, and select Gentle/Balanced/Fast work pacing.
- **Wallet:** available/pending/maturing balances, send with exact funded fee
  review before signing, receive addresses and QR/payment requests, transaction
  details, CSV export of the latest 100 entries, wallet-file backup and restore.
- **Security:** encryption, passphrase changes, wallet lock, sign/verify
  messages and watch-only import. Restore preserves the prior wallet file.
- **Node:** live sync, connections, estimated network hashrate, peer add/
  disconnect/ban, block and transaction inspection, recent logs and full RPC.
- **Settings:** appearance, network, database cache, retained block-file target,
  connection limit and upload target. Node changes restart gracefully.

Work pace limits nonce attempts per request and inserts rests. It is not a CPU
percentage cap. Stop stops scheduling new work and waits for an outstanding
batch to finish; the upstream RPC cannot cancel that request midway. Session
rewards describe this run, while actual maturity is shown in the wallet.

Backups are first created by Core in its internal backup directory and then
copied atomically to the chosen destination. Imports/older restores can need
historical blocks that a pruned node no longer holds. All specialist Core
commands remain available through the advanced RPC tool. UAP marketplace/indexer
services are not bundled or started as part of this desktop client.

## Data and process ownership

App data uses Qt's per-user application-data location under
`WhippetDesktop/Whippet`; Settings can open it. Override with `--data-dir PATH`.
Each network has its own wallet and chain under `chain/<network>`.
Loopback cookie-authenticated RPC uses ports 33675 / 43675 / 53675, separate
from Core's defaults. Only one app may manage a data folder at once. The app
starts only its bundled daemon and requests a clean stop on exit. It never
opens or replaces another installation's wallet automatically.

## Build

Prerequisites: CMake 3.22+, C++17 compiler, Python 3 for the *build helper*, Qt
6.4+ Widgets/Network/Test. Windows/macOS recipes pin Qt 6.8.3. The small QR
library is vendored at v1.8.0 with its MIT notice. Node downloads are pinned by
archive SHA-256 and by the SHA-256 of the upstream checksum manifest itself.

Windows (Visual Studio 2022 and Inno Setup 6):

```powershell
./desktop/packaging/build-windows.ps1 -QtPrefix C:/Qt/6.8.3/msvc2022_64
```

Linux with `qt6-base-dev`, `qt6-base-dev-tools`, `dpkg-dev`, `xvfb`:

```sh
xvfb-run -a bash desktop/packaging/build-unix.sh
```

macOS with Qt installed:

```sh
bash desktop/packaging/build-unix.sh -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/macos
```

Output packages are placed in `desktop/dist`. The GitHub **Desktop installers**
workflow builds each platform on its native runner, tests it, and uploads
packages with SHA-256 sums. Nothing is automatically published as a release.

## Verification

`desktop-tests` checks decimal amounts, mining activation boundaries, readiness
gates, missing cookie behavior, authentication and RPC error handling.
`desktop-ui-tests` uses a real isolated regtest daemon to verify startup,
compact geometry, mining across height 20, stopping, navigation, theme changes
and graceful shutdown; it captures actual rendered screenshots.
`tests/integration.py NODE` mines 66 regtest blocks, funds/signs/broadcasts and
confirms a payment, checks backup creation and clean daemon shutdown.

The Windows installer is built and tested locally. Other platform results must
be checked in the native build workflow; the presence of a recipe alone is not
evidence that a package was built or tested.
