export const BOARD_SCHEMA_VERSION = 1 as const;

export type PageSourceKind = "blank" | "image" | "pdf" | "powerpoint" | "slidev";

export type BoardPage = {
  id: string;
  name: string;
  scene: string | null;
  thumbnail?: string;
  source: {
    kind: PageSourceKind;
    fileName?: string;
    pageNumber?: number;
  };
};

export type BoardDocument = {
  schemaVersion: typeof BOARD_SCHEMA_VERSION;
  id: string;
  title: string;
  createdAt: string;
  updatedAt: string;
  activePageId: string;
  pages: BoardPage[];
};

export type WelcomeAsset = {
  id: string;
  name: string;
  dataUrl: string;
};

export type WelcomeContentMode = "builtin" | "board-page" | "images";

export type WelcomePreferences = {
  enabled: boolean;
  idleMinutes: number;
  slideSeconds: number;
  contentMode: WelcomeContentMode;
  boardPageId: string | null;
  headline: string;
  subline: string;
  assets: WelcomeAsset[];
};

export const DEFAULT_WELCOME_PREFERENCES: WelcomePreferences = {
  enabled: true,
  idleMinutes: 10,
  slideSeconds: 12,
  contentMode: "builtin",
  boardPageId: null,
  headline: "欢迎莅临",
  subline: "无锡南荒科技有限公司",
  assets: [],
};

const createId = () =>
  globalThis.crypto?.randomUUID?.() ??
  `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;

export const createBlankPage = (index = 1): BoardPage => ({
  id: createId(),
  name: `白板 ${index}`,
  scene: null,
  source: { kind: "blank" },
});

export const createBoardDocument = (): BoardDocument => {
  const now = new Date().toISOString();
  const firstPage = createBlankPage();

  return {
    schemaVersion: BOARD_SCHEMA_VERSION,
    id: createId(),
    title: "未命名白板",
    createdAt: now,
    updatedAt: now,
    activePageId: firstPage.id,
    pages: [firstPage],
  };
};

const isSerializedScene = (value: string | null): boolean => {
  if (value === null) return true;
  try {
    const scene = JSON.parse(value) as { elements?: unknown; files?: unknown };
    return (
      Boolean(scene) &&
      typeof scene === "object" &&
      Array.isArray(scene.elements) &&
      Boolean(scene.files) &&
      typeof scene.files === "object"
    );
  } catch {
    return false;
  }
};

export const isBoardDocument = (value: unknown): value is BoardDocument => {
  if (!value || typeof value !== "object") {
    return false;
  }

  const candidate = value as Partial<BoardDocument>;
  const sourceKinds = new Set<PageSourceKind>([
    "blank",
    "image",
    "pdf",
    "powerpoint",
    "slidev",
  ]);
  return (
    candidate.schemaVersion === BOARD_SCHEMA_VERSION &&
    typeof candidate.id === "string" &&
    typeof candidate.title === "string" &&
    typeof candidate.createdAt === "string" &&
    typeof candidate.updatedAt === "string" &&
    typeof candidate.activePageId === "string" &&
    Array.isArray(candidate.pages) &&
    candidate.pages.length > 0 &&
    candidate.pages.every(
      (page) =>
        typeof page?.id === "string" &&
        typeof page?.name === "string" &&
        (page?.scene === null || typeof page?.scene === "string") &&
        isSerializedScene(page.scene) &&
        Boolean(page?.source) &&
        sourceKinds.has(page.source.kind),
    ) &&
    candidate.pages.some((page) => page.id === candidate.activePageId)
  );
};

export const sanitizeWelcomePreferences = (value: unknown): WelcomePreferences => {
  if (!value || typeof value !== "object") {
    return DEFAULT_WELCOME_PREFERENCES;
  }

  const candidate = value as Partial<WelcomePreferences>;
  const assets = Array.isArray(candidate.assets)
    ? candidate.assets.filter(
        (asset): asset is WelcomeAsset =>
          Boolean(asset) &&
          typeof asset.id === "string" &&
          typeof asset.name === "string" &&
          typeof asset.dataUrl === "string" &&
          asset.dataUrl.startsWith("data:image/") &&
          asset.dataUrl.length <= 21 * 1024 * 1024,
      ).slice(0, 20)
    : [];

  return {
    enabled:
      typeof candidate.enabled === "boolean"
        ? candidate.enabled
        : DEFAULT_WELCOME_PREFERENCES.enabled,
    idleMinutes:
      typeof candidate.idleMinutes === "number" &&
      candidate.idleMinutes >= 0 &&
      candidate.idleMinutes <= 1_440
        ? candidate.idleMinutes
        : DEFAULT_WELCOME_PREFERENCES.idleMinutes,
    slideSeconds:
      typeof candidate.slideSeconds === "number" &&
      candidate.slideSeconds >= 3 &&
      candidate.slideSeconds <= 3_600
        ? candidate.slideSeconds
        : DEFAULT_WELCOME_PREFERENCES.slideSeconds,
    contentMode:
      candidate.contentMode === "board-page" ||
      candidate.contentMode === "images" ||
      candidate.contentMode === "builtin"
        ? candidate.contentMode
        : assets.length > 0
          ? "images"
          : DEFAULT_WELCOME_PREFERENCES.contentMode,
    boardPageId:
      typeof candidate.boardPageId === "string" && candidate.boardPageId.length > 0
        ? candidate.boardPageId
        : null,
    headline:
      typeof candidate.headline === "string"
        ? candidate.headline.slice(0, 40)
        : DEFAULT_WELCOME_PREFERENCES.headline,
    subline:
      typeof candidate.subline === "string"
        ? candidate.subline.slice(0, 80)
        : DEFAULT_WELCOME_PREFERENCES.subline,
    assets,
  };
};

export const newBoardId = createId;
