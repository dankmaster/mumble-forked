# Unsigned private-community input-enhancement package

This is the concrete pre-Azure handoff for the private Windows test community.
It creates one portable ZIP from an already qualified staged payload. It does
not build, tune, sign, publish, create a GitHub Release, or update a channel
pointer.

The artifact is deliberately named
`mumble-1.7.<build>-<sha12>-UNSIGNED-PRIVATE-COMMUNITY.zip`. It is not a public
release. Windows may warn because Mumble's PE files and the ZIP have not gone
through production Authenticode/Azure signing.

## Required immutable inputs

The packaging runner must hold all of the following from the same candidate:

- the clean source and build roots used to create the candidate receipt;
- the complete staged Windows payload;
- a live-valid `candidate-build-receipt.json` and its protected SHA-256;
- the passing schema-v2 `core_release` measured-quality attestation, its eight
  referenced master/nightly qualification sidecars, and its protected SHA-256;
- `input-models.json`, `input-recipes.json`, and their raw 64-byte Ed25519
  signatures in the stage;
- an unexpired canonical `input-enhancement-policy.json` and its raw 64-byte
  Ed25519 signature in the stage; and
- the operator/test Ed25519 public key embedded in that exact client.

The community key is separate from the future production key. Packaging never
receives a private key. Azure/OIDC/private-key environment variables make the
script fail before it creates an output directory.

## Exact binding

`new-input-enhancement-private-community-package.ps1` verifies signatures and
policy with the existing release trust module, then calls
`private_community_package.py`. The Python gate revalidates the candidate
receipt live, including the source, build graph, Ninja freshness, toolchain,
test gates, client hash, complete stage tree, embedded public key, manifests,
and policy.

The core qualification is bound in two layers:

1. The full current stage must equal the candidate receipt's payload identity.
2. The same stage after excluding exactly these six signed bootstrap files
   must equal `protectedBuildIdentity.staged_payload_sha256` from the protected
   core qualification:
   `input-models.json[.sig]`, `input-recipes.json[.sig]`, and
   `input-enhancement-policy.json[.sig]`.

No other post-quality mutation is permitted. The model and recipe manifest
bytes must equal the manifests named by core evidence; every model asset is
rehash-checked; all four protected runner identities and the 500/5,000 case
counts are rechecked; every quality/Original sidecar hash and its core identity
must match. A stale DLL, different client, changed model, expired policy,
missing signature, altered evidence file, incomplete runner matrix, or added
stage file fails closed.

## Output

The new output directory contains exactly:

- the portable ZIP;
- `<name>.receipt.json`, which binds the ZIP hash to source, build, full stage,
  candidate receipt, and core qualification; and
- `<name>.sha256`.

The ZIP has deterministic entry ordering and timestamps and contains:

- `app/`, the complete qualified stage;
- `metadata/private-community-package.json`, a complete hash/size inventory;
  and
- `metadata/README-UNSIGNED-PRIVATE-COMMUNITY.txt`, the tester warning.

Creation is write-once. Existing outputs are never overwritten. The finished
archive is immediately expanded logically and every entry is rehashed. The
manual workflow uploads only these three files as a short-lived Actions
artifact, downloads them on a separate hosted runner, and repeats exact ZIP
verification. It has only `actions: read` and `contents: read`.

## Operator command

```powershell
.\scripts\windows\new-input-enhancement-private-community-package.ps1 `
  -SourceRoot D:\protected\candidate-source `
  -SourceSha <40-hex-master-sha> `
  -BuildNumber <positive-build> `
  -BuildRoot D:\protected\candidate-build `
  -StageRoot D:\protected\candidate-stage `
  -CandidateBuildReceiptPath D:\protected\evidence\candidate-build-receipt.json `
  -CandidateBuildReceiptSha256 <64-hex> `
  -CoreMeasuredAttestationPath D:\protected\evidence\measured-quality-attestation.json `
  -CoreMeasuredAttestationSha256 <64-hex> `
  -Ed25519PublicKeyHex <operator-test-public-key-hex> `
  -AllowedOutputParent D:\private-community-output `
  -OutputRoot D:\private-community-output\candidate-<build>
```

The output parent must already exist and the output root must be a new direct
child outside source, build, and stage.

## Why there is no MSI yet

The existing MSI release path intentionally requires Authenticode-verified PE
payloads and a signed MSI. Reusing it here would either weaken that gate or
mislabel a test-certificate installer. The pre-Azure community handoff is
therefore portable ZIP only. MSI is added only after the qualified bytes enter
the production signing stage.
