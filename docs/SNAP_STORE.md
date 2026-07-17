# Snap Store distribution

The Snap Store distributes Linux applications only. Windows and macOS packages use their own distribution channels.

## Build locally

Install Snapcraft and build:

```bash
sudo snap install snapcraft --classic
./scripts/configure-app.sh
snapcraft
```

Install the resulting package for testing:

```bash
sudo snap install --dangerous ./getbible-life_1.0.0_amd64.snap
```

Remove it completely, including application data:

```bash
sudo snap remove --purge getbible-life
```

## Reserve the Store name

Log in at the Snapcraft publisher portal and register the name configured in `snap.name`. The default is `getbible-life`.

The Store account that owns the name must also be the account used to create workflow credentials.

## Create restricted credentials

On a trusted machine:

```bash
snapcraft login
snapcraft export-login \
  --snaps getbible-life \
  --channels edge,beta,candidate,stable \
  --acls package_access,package_push,package_release \
  snapcraft-store-credentials.txt
```

Add the complete file contents as the GitHub Actions repository secret `SNAPCRAFT_STORE_CREDENTIALS`.

Do not commit this credential. Delete the local file after saving the secret.

## Automatic publication

The release workflow first creates the GitHub Release and uploads every platform package. It then runs a separate `publish-snap` job.

With valid `SNAPCRAFT_STORE_CREDENTIALS`, the Snap is released to the channel selected from the version:

- alpha/dev → edge;
- beta → beta;
- release candidate → candidate;
- final semantic version → stable.

Without credentials, the final Store job fails clearly while the GitHub Release and downloadable packages remain intact.

## Manual publication

A built package can also be published locally:

```bash
snapcraft upload --release=edge getbible-life_1.0.0_amd64.snap
```

Promote an already uploaded revision after testing:

```bash
snapcraft release getbible-life REVISION candidate
snapcraft release getbible-life REVISION stable
```

## Forks

A fork should reserve its own Snap name and override `snap.name` through `WEBAPP_CONFIG_JSON` or `WEBAPP_SNAP_NAME`. Store credentials are scoped to that reserved name and must be configured independently in the fork.
