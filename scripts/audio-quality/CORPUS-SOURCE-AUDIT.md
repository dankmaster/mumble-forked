# Corpus source audit

Verified 2026-07-17. This note records why some sources in
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
- Inventory schema v3 binds each extracted/converted WAV's byte size and
  SHA-256 to the source archive SHA-256. The renderer re-hashes the WAV before
  use, so archive substitution or post-inventory file changes fail closed.

## DEMAND environmental noise

- Primary record: [DEMAND on Zenodo](https://zenodo.org/records/1227121). Its
  description explicitly applies CC-BY-SA-3.0 to the work, audio data and
  document. This project conservatively uses that term even though other Zenodo
  metadata is less restrictive.
- Eighteen scene archives are independently pinned by official MD5, locally
  verified SHA-256 and byte size in `corpus-lock.json`: DKITCHEN, DLIVING,
  DWASHING, NFIELD, NPARK, NRIVER, OHALLWAY, OMEETING, OOFFICE, PCAFETER,
  PRESTO, PSTATION, SCAFE, SPSQUARE, STRAFFIC, TBUS, TCAR and TMETRO. Each
  contains sixteen
  separate mono channel WAVs. Product training and redistribution remain
  blocked by project policy; only private local evaluation and mixture
  generation are approved.
- The frozen identifier-only seed `mumble-community-master-v2-00909005` maps
  six DEMAND scene groups to each split. DKITCHEN/NFIELD/OHALLWAY/OMEETING/
  SPSQUARE/TMETRO map to tuning; DWASHING/OOFFICE/PRESTO/PSTATION/TBUS/TCAR
  map to validation; DLIVING/NPARK/NRIVER/PCAFETER/SCAFE/STRAFFIC map to
  holdout. No audio, metric, model output, recipe result or profile output
  participated in the seed search, and an exact 6/6/6 regression test protects
  the mapping.
- The builder selects `ch01.wav` and the fixed 60,000–120,000 ms window from
  every archive, converts it to mono 48 kHz PCM16 and binds both source-member
  and output hashes. Holdout conversion is blind, deterministic corpus
  preparation only: it may be hashed into inventory but must not be listened
  to, mixture-rendered, scored, or used for a recipe decision before final
  release qualification.
- Together with the nine independent OpenSLR28 RVB2014 isotropic environments,
  this is enough for the master-quality floor in every split without treating
  array channels or time windows as separate groups. The CC0-only FSD50K
  expansion below supplies the additional independently uploaded groups and
  missing classes needed by nightly.

## FSD50K evaluation CC0 subset

- Primary record: [FSD50K on Zenodo](https://zenodo.org/records/4060432), DOI
  `10.5281/zenodo.4060432`. The collection is CC-BY-4.0, but individual
  Freesound clips retain their own terms. Selection therefore fails closed
  unless the exact clip metadata URI is CC0-1.0.
- Both official split-ZIP volumes are independently locked: `FSD50K.eval_audio.z01`
  is 3,221,225,472 bytes with SHA-256
  `a1b776a5a9466a0cf17d0cb9a2a98f641454084f0292f511718aacb48e6c401d`;
  `FSD50K.eval_audio.zip` is 3,037,675,767 bytes with SHA-256
  `e3d78e77115ff8d8df1ff3dae7ca5cd5864924c054561f61ba836764254bf50e`.
  The 6,700,838-byte metadata and 334,701-byte ground-truth ZIPs are separately
  pinned by SHA-256 in the lock.
- The tracked selection contains 21 unique uploader groups: six tuning, seven
  validation and eight holdout. It fills keyboard/rain/music-TV in tuning;
  rain/wind/handling/music-TV in validation; and hum/keyboard/handling/
  competing-speech in holdout, then adds identifier-ranked groups without
  inventing independence.
- The builder reads uploader names only transiently. Inventory group IDs are
  `fsd50k-uploader-` plus SHA-256 of the domain-separated uploader value;
  uploader names, titles and descriptions are never persisted. It rechecks
  both the exact CC0 URI and a direct ground-truth label before decoding a clip.
- The selection used metadata and identifiers only. No FSD50K holdout audio was
  decoded, rendered, listened to or scored during this audit. Holdout remains
  sealed until final qualification.

## Google FLEURS Swedish (`sv_se`)

- Primary repository: [Google FLEURS on Hugging Face](https://huggingface.co/datasets/google/fleurs).
  The corpus is documented by the [FLEURS paper](https://arxiv.org/abs/2205.12446)
  and the dataset card declares CC-BY-4.0.
- This project pins dataset revision
  [`d7c758a6dceecd54a98cac43404d3d576e721f07`](https://huggingface.co/datasets/google/fleurs/tree/d7c758a6dceecd54a98cac43404d3d576e721f07/data/sv_se),
  predating the later repository-wide Parquet conversion. The language-local
  `train.tar.gz` is 1,459,456,451 bytes and its Git LFS object SHA-256 is
  `04a18cb93720cfed1ed992abebe977ac3880cab17ec7cf9cb5afe77d439988d3`.
  A clean local download matched both values.
- The exact-revision `train.tsv` sidecar is 1,313,805 bytes with independently
  verified SHA-256
  `e379de2d6d9ba18d5e75a2e83c75b9cf59aa3760524cd49c468e4c87c9a1e6bf`.
  It contains 2,385 rows for 1,373 sentence IDs and binds each WAV filename to
  Swedish raw/normalized transcripts and its 16 kHz sample count.
- FLEURS does not expose persistent speaker IDs. The paper does state that the
  three recordings collected for a sentence are made by three different native
  speakers. To preserve that guarantee without voice clustering or invented
  cross-sentence identities, the builder selects all three recordings from one
  sentence only. For the frozen split seed, sentence ID `8` is the first numeric
  eligible sentence whose three at-least-six-second recording IDs map one each
  to tuning, validation and holdout. No audio quality score or model output is
  consulted.
- A real local materialization verified 2,385 float32 WAV members against the
  TSV, converted only the selected three to mono PCM16 48 kHz, bound their
  transcript hashes, and produced two-language coverage in every release split.
  Raw archives, extracted clips and mixtures remain ignored local evidence and
  are never redistributed in Mumble artifacts.

## RixVox v1 Swedish expansion

- Primary source: [KBLab RixVox](https://huggingface.co/datasets/KBLab/rixvox),
  exact revision `9b2b6066ee184faf436363ff0823f2e465ccfb31`. The dataset card
  declares CC-BY-4.0 and attributes the Swedish Parliament.
- Only two audio shards are required by the identifier-only reservoir:
  `dev_0.tar.gz` is 4,042,055,183 bytes with SHA-256
  `6c885473594ac2f81ab8cb790103803181eb998dc32cbbb63cee51b93a84c9ab`;
  `test_0.tar.gz` is 4,011,846,841 bytes with SHA-256
  `7c75945e820a8f8f5dea2b24183c6c97faa7df0028908935f3f4171d82f07f3c`.
  Exact-revision dev/test Parquet metadata are separately locked at 1,708,010
  and 2,028,178 bytes.
- Stable source speaker identity is `intressent_id`, but raw IDs and all name,
  party, gender, birth-year and demographic fields are forbidden from the
  inventory. The builder uses only a domain-separated SHA-256 pseudonym. The
  tracked selection contains five unique Swedish speaker groups per frozen
  split and one transcripted utterance of at least six seconds per group.
- `nightly-corpus-selection-v1.json` is the privacy-scrubbed derivative of the
  exact metadata. Its selection is identifier-only and binds source shard,
  member, transcript, duration, group pseudonym and utterance-rank hash. The
  builder verifies archive membership/duration and binds the selection file's
  own SHA-256; it never needs to persist the raw metadata identity fields.

## OpenSLR SLR12 LibriSpeech test-clean expansion

- Official archive: [test-clean.tar.gz](https://www.openslr.org/resources/12/test-clean.tar.gz),
  346,663,984 bytes, SHA-256
  `39fde525e59672dc6d1551919b1478f724438a95aa55f874b576be21967e6c23`,
  licensed CC-BY-4.0 by [OpenSLR SLR12](https://www.openslr.org/12/).
- The source is deliberately evaluation-only. Two transcripted speakers per
  frozen split are selected by identifier: tuning 1995/5683, validation
  1221/4992 and holdout 3729/8463. They are speaker-disjoint from the existing
  mini LibriSpeech dev-clean source. Utterances are ranked by identifiers only.

## Common Voice Swedish 26.0

- Primary release record: [Common Voice Scripted Speech 26.0 - Swedish](https://mozilladatacollective.com/datasets/cmqintw0q00xwnr07mjjzpe61).
- Versioned upstream metadata: [`cv-corpus-26.0-2026-06-12.json`](https://raw.githubusercontent.com/common-voice/cv-dataset/f99d8239d2796131b73ac99f92ee7cb4443bf3ba/datasets/scripted-speech/cv-corpus-26.0-2026-06-12.json), pinned at repository commit `f99d8239d2796131b73ac99f92ee7cb4443bf3ba` (Git blob `533fdf22b404805976c950894e3961d2398bf56b`).
- The `sv-SE` release row records 50,259 clips, 1,281,634,603 bytes and archive SHA-256 `fc6e3a1c28e05f361badf51b0b8e1770db0d3012b02087b382b22ccd92bb0051`.
- License is CC0-1.0. Training and local evaluation are allowed. The [current Common Voice terms](https://commonvoice.mozilla.org/terms) and MDC record prohibit speaker identification and re-hosting/re-sharing, so redistribution is blocked in this project.
- Remaining work: implement an explicit authenticated MDC acquisition flow that records acceptance of the current terms and verifies the archive against the locked size and SHA-256. Until then, no `artifact_path` is present and the generic fetcher cannot download it.

## VoxPopuli Swedish rejection

- Audited official revision:
  [`f7a3bb98d664e1d031763ec4f7639c4a530c64e9`](https://github.com/facebookresearch/voxpopuli/tree/f7a3bb98d664e1d031763ec4f7639c4a530c64e9).
  VoxPopuli data is CC0, with the raw European Parliament recordings also
  subject to the linked Parliament legal notice.
- It is not usable for this transcripted Swedish-speaker expansion. Swedish is
  absent from the official ASR languages; `unlabelled_v2` supplies neither a
  transcript nor `speaker_id`; and `s2s_sv` has no human Swedish target
  transcript while `src_speaker_id` identifies the source-language speaker,
  not a Swedish interpreter.
- Treating those records as Swedish speaker identities would create false
  split independence and could not support Swedish WER. The source is therefore
  explicitly rejected in the lock for this purpose, even though it may be
  reconsidered as a separately audited CC0 training source later.

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
