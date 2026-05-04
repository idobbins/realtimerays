export type RecordingProfileId = "preview" | "balanced" | "high" | "near-lossless";

export type RecordingProfile = {
  id: RecordingProfileId;
  label: string;
  menuLabel: string;
  detail: string;
  fps: number;
  videoBitsPerSecond: number;
  preferredMimeTypes: string[];
};

export const recordingProfiles: RecordingProfile[] = [
  {
    id: "preview",
    label: "Preview",
    menuLabel: "Preview - 30 fps / 5 Mbps",
    detail: "small files for quick captures",
    fps: 30,
    videoBitsPerSecond: 5_000_000,
    preferredMimeTypes: ["video/webm;codecs=vp8", "video/webm"],
  },
  {
    id: "balanced",
    label: "Balanced",
    menuLabel: "Balanced - 30 fps / 10 Mbps",
    detail: "default quality with manageable file sizes",
    fps: 30,
    videoBitsPerSecond: 10_000_000,
    preferredMimeTypes: ["video/webm;codecs=vp9", "video/webm;codecs=vp8", "video/webm"],
  },
  {
    id: "high",
    label: "High",
    menuLabel: "High - 60 fps / 24 Mbps",
    detail: "smooth motion with cleaner gradients",
    fps: 60,
    videoBitsPerSecond: 24_000_000,
    preferredMimeTypes: ["video/webm;codecs=vp9", "video/webm;codecs=vp8", "video/webm"],
  },
  {
    id: "near-lossless",
    label: "Near-lossless",
    menuLabel: "Near-lossless - 60 fps / 80 Mbps",
    detail: "very large files, visually conservative encode",
    fps: 60,
    videoBitsPerSecond: 80_000_000,
    preferredMimeTypes: ["video/webm;codecs=vp9", "video/webm;codecs=vp8", "video/webm"],
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
