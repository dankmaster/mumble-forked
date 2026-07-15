# Corpus source audit

Verified 2026-07-15. This note records why some sources in
`corpus-lock.json` intentionally have no `artifact_path`. The fetcher treats
that omission as a hard block.

## OpenSLR SLR28 RIR and noise database

- Primary record: [OpenSLR SLR28](https://www.openslr.org/28/). The record and
  its machine-readable [`info.txt`](https://www.openslr.org/resources/28/info.txt)
  explicitly identify the database license as Apache-2.0 and describe real and
  simulated RIRs, real isotropic noise, and point-source noise.
- Official archive: [`rirs_noises.zip`](https://www.openslr.org/resources/28/rirs_noises.zip).
  OpenSLR publishes MD5 `e6f48e257286e05de56413b4779d8ffb` in its
  [`checksum.md5`](https://www.openslr.org/resources/28/checksum.md5).
- A clean local audit download was 1,311,166,223 bytes, matched that official
  MD5, and independently produced SHA-256
  `3b50cfde915b3984738169b4beb341e9f6b8062ae4c2076146c5db71c2c05dc7`.
  That byte size and stronger digest are the fetch lock.
- Archive inspection found 61,260 PCM16 WAV files, all at 16 kHz, with channel
  counts 1, 2, 8, 16, or 30. The usable noise portion includes 843
  point-source and 92 real isotropic-noise WAVs; the remainder includes real
  and simulated RIRs.
- Project policy permits private local evaluation and mixture generation only.
  Product training and redistribution remain blocked even though Apache-2.0 is
  commercially compatible. Extracted audio stays outside Git and CI artifacts.
- Inventory schema v2 binds each extracted/converted WAV's byte size and
  SHA-256 to the source archive SHA-256. The renderer re-hashes the WAV before
  use, so archive substitution or post-inventory file changes fail closed.

## Common Voice Swedish 26.0

- Primary release record: [Common Voice Scripted Speech 26.0 - Swedish](https://mozilladatacollective.com/datasets/cmqintw0q00xwnr07mjjzpe61).
- Versioned upstream metadata: [`cv-corpus-26.0-2026-06-12.json`](https://raw.githubusercontent.com/common-voice/cv-dataset/f99d8239d2796131b73ac99f92ee7cb4443bf3ba/datasets/scripted-speech/cv-corpus-26.0-2026-06-12.json), pinned at repository commit `f99d8239d2796131b73ac99f92ee7cb4443bf3ba` (Git blob `533fdf22b404805976c950894e3961d2398bf56b`).
- The `sv-SE` release row records 50,259 clips, 1,281,634,603 bytes and archive SHA-256 `fc6e3a1c28e05f361badf51b0b8e1770db0d3012b02087b382b22ccd92bb0051`.
- License is CC0-1.0. Training and local evaluation are allowed. The [current Common Voice terms](https://commonvoice.mozilla.org/terms) and MDC record prohibit speaker identification and re-hosting/re-sharing, so redistribution is blocked in this project.
- Remaining work: implement an explicit authenticated MDC acquisition flow that records acceptance of the current terms and verifies the archive against the locked size and SHA-256. Until then, no `artifact_path` is present and the generic fetcher cannot download it.

## VCTK 0.92

- Primary record: [University of Edinburgh DataShare, DOI 10.7488/ds/2645](https://datashare.ed.ac.uk/items/30e7453c-9ea8-48b4-8e18-f96d0dc62928).
- The record describes 110 speakers and mono 48 kHz material. Its archive bitstream metadata reports `VCTK-Corpus-0.92.zip`, 11,747,302,977 bytes and MD5 `8a6ba2946b36fcbef0212cad601f4bfa`.
- The current [license bitstream](https://datashare.ed.ac.uk/server/api/core/bitstreams/956a1688-0b59-428c-8a2f-10837433dde3/content) is CC-BY-4.0. This supersedes older secondary descriptions that call VCTK ODC-By-1.0.
- The lock's SHA-256 `468ed794251d6662cb1b4666c6163516b6bdf8bb14eb09daf3e82aeba2014174` covers only the 1,829-byte official [bitstream metadata response](https://datashare.ed.ac.uk/server/api/core/bitstreams/535f4286-e54c-4038-838c-a02285e32cb2), not the ZIP.
- Remaining work: a trusted corpus runner must acquire the archive once, verify the published size and MD5, calculate SHA-256, and independently confirm the result before an `artifact_path` can be added. Redistribution remains subject to a fixture-specific attribution review.

## DNS Challenge 5

- Primary repository revision: [`591184a9fcb2cbdec02520fed81a32bbbf9d73ff`](https://github.com/microsoft/DNS-Challenge/tree/591184a9fcb2cbdec02520fed81a32bbbf9d73ff).
- The repository documents roughly 550 GB of archives (about 1 TB unpacked), plus a mixture of LibriVox, PTDB-TUG, Edinburgh speech, VocalSet, CREMA-D, VoxCeleb2, VCTK, AudioSet, Freesound, DEMAND and OpenSLR material.
- Repository documentation is CC-BY-4.0 and code is MIT, but the datasets retain each upstream source's original terms. The aggregate bundle therefore has no truthful single SPDX license.
- The formerly linked DNS5 per-file SHA-1 inventory was unavailable during this audit, and the download scripts do not provide a reviewed SHA-256 lock for every archive.
- Remaining work: split DNS5 into component records, approve each component's current license and permitted roles, pin every selected archive by size and SHA-256, and keep speakers/noises/rooms disjoint across tuning, validation and release holdout. Until all of that is complete, DNS5 remains explicitly excluded from training, evaluation and redistribution.
