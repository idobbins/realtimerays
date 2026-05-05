export type RecordingProfileId = "preview" | "balanced" | "high" | "near-lossless";

export type RecordingProfile = {
  id: RecordingProfileId;
  label: string;
  menuLabel: string;
  detail: string;
  fps: number;
  videoBitsPerSecond: number;
};

export type RecordingCodecId = "av1" | "av01" | "vp9" | "vp8" | "webm";

export type RecordingCodec = {
  id: RecordingCodecId;
  label: string;
  menuLabel: string;
  mimeType: string;
  detail: string;
};

export const recordingCodecs: RecordingCodec[] = [
  {
    id: "av1",
    label: "AV1 WebM",
    menuLabel: "AV1 WebM",
    mimeType: "video/webm;codecs=av1",
    detail: "AV1 codec in a WebM container",
  },
  {
    id: "av01",
    label: "AV1 WebM",
    menuLabel: "AV1 WebM alt",
    mimeType: "video/webm;codecs=av01",
    detail: "alternate AV1 codec string in a WebM container",
  },
  {
    id: "vp9",
    label: "VP9 WebM",
    menuLabel: "VP9 WebM",
    mimeType: "video/webm;codecs=vp9",
    detail: "VP9 codec in a WebM container",
  },
  {
    id: "vp8",
    label: "VP8 WebM",
    menuLabel: "VP8 WebM",
    mimeType: "video/webm;codecs=vp8",
    detail: "VP8 codec in a WebM container",
  },
  {
    id: "webm",
    label: "WebM",
    menuLabel: "WebM",
    mimeType: "video/webm",
    detail: "browser default WebM encoder",
  },
];
export const defaultRecordingCodecId = "av1" satisfies RecordingCodecId;

export const recordingProfiles: RecordingProfile[] = [
  {
    id: "preview",
    label: "Preview",
    menuLabel: "Preview - 30 fps / 12 Mbps",
    detail: "small files for quick captures",
    fps: 30,
    videoBitsPerSecond: 12_000_000,
  },
  {
    id: "balanced",
    label: "Balanced",
    menuLabel: "Balanced - 30 fps / 40 Mbps",
    detail: "clean default quality with manageable file sizes",
    fps: 30,
    videoBitsPerSecond: 40_000_000,
  },
  {
    id: "high",
    label: "High",
    menuLabel: "High - 60 fps / 100 Mbps",
    detail: "smooth motion with high-detail gradients",
    fps: 60,
    videoBitsPerSecond: 100_000_000,
  },
  {
    id: "near-lossless",
    label: "Near-lossless",
    menuLabel: "Near-lossless - 60 fps / 150 Mbps",
    detail: "very large files, maximum browser encoder target",
    fps: 60,
    videoBitsPerSecond: 150_000_000,
  },
];

export const defaultRecordingProfileId = "balanced" satisfies RecordingProfileId;

export function getRecordingProfile(profileId: RecordingProfileId) {
  return (
    recordingProfiles.find((profile) => profile.id === profileId) ??
    recordingProfiles.find((profile) => profile.id === defaultRecordingProfileId) ??
    recordingProfiles[0]
  );
}

export function getRecordingCodec(codecId: RecordingCodecId) {
  return (
    recordingCodecs.find((codec) => codec.id === codecId) ??
    recordingCodecs.find((codec) => codec.id === defaultRecordingCodecId) ??
    recordingCodecs[0]
  );
}

export function getSupportedRecordingCodecs() {
  if (typeof MediaRecorder === "undefined") {
    return [];
  }

  return recordingCodecs.filter((codec) => MediaRecorder.isTypeSupported(codec.mimeType));
}
