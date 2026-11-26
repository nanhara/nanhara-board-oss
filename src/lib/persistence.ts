import {
  createBoardDocument,
  DEFAULT_WELCOME_PREFERENCES,
  isBoardDocument,
  sanitizeWelcomePreferences,
  type BoardDocument,
  type WelcomePreferences,
} from "./board-model";

const CURRENT_DOCUMENT_KEY = "nanhara-board:document:current:v1";
const BACKUP_DOCUMENT_KEY = "nanhara-board:document:backup:v1";
const WELCOME_PREFERENCES_KEY = "nanhara-board:welcome:v1";
const BACKUP_INTERVAL_MS = 30_000;

let writeQueue: Promise<void> = Promise.resolve();
let lastBackupAt = 0;

type NativeStatePair = {
  current: string | null;
  backup: string | null;
};

const isNativeDesktop = () =>
  typeof window !== "undefined" && Boolean(window.__TAURI_INTERNALS__);

const parseJson = (value: string | null): unknown => {
  if (!value) return null;
  try {
    return JSON.parse(value) as unknown;
  } catch {
    return null;
  }
};

const invokeNative = async <T>(
  command: string,
  payload?: Record<string, unknown>,
): Promise<T> => {
  const { invoke } = await import("@tauri-apps/api/core");
  return invoke<T>(command, payload);
};

const loadBrowserValue = async <T>(key: string): Promise<T | undefined> => {
  const { get } = await import("idb-keyval");
  return get<T>(key);
};

const saveBrowserValue = async (key: string, value: unknown): Promise<void> => {
  const { set } = await import("idb-keyval");
  await set(key, value);
};

export type LoadDocumentResult = {
  document: BoardDocument;
  recovered: boolean;
};

export const loadBoardDocument = async (): Promise<LoadDocumentResult> => {
  const nativePair = isNativeDesktop()
    ? await invokeNative<NativeStatePair>("load_board_documents")
    : null;
  const current = nativePair
    ? parseJson(nativePair.current)
    : await loadBrowserValue<unknown>(CURRENT_DOCUMENT_KEY);
  if (isBoardDocument(current)) {
    return { document: current, recovered: false };
  }

  const backup = nativePair
    ? parseJson(nativePair.backup)
    : await loadBrowserValue<unknown>(BACKUP_DOCUMENT_KEY);
  if (isBoardDocument(backup)) {
    return { document: backup, recovered: true };
  }

  return { document: createBoardDocument(), recovered: false };
};

export const persistBoardDocument = (document: BoardDocument): Promise<void> => {
  const task = writeQueue.then(async () => {
    if (isNativeDesktop()) {
      await invokeNative<void>("save_board_document", {
        payload: JSON.stringify(document),
      });
      return;
    }

    const now = Date.now();
    if (now - lastBackupAt >= BACKUP_INTERVAL_MS) {
      const previous = await loadBrowserValue<unknown>(CURRENT_DOCUMENT_KEY);
      if (isBoardDocument(previous)) {
        await saveBrowserValue(BACKUP_DOCUMENT_KEY, previous);
      }
      lastBackupAt = now;
    }

    await saveBrowserValue(CURRENT_DOCUMENT_KEY, document);
  });

  writeQueue = task.catch(() => undefined);
  return task;
};

export const loadWelcomePreferences = async (): Promise<WelcomePreferences> => {
  const nativePair = isNativeDesktop()
    ? await invokeNative<NativeStatePair>("load_welcome_preferences")
    : null;
  const current = nativePair
    ? parseJson(nativePair.current)
    : await loadBrowserValue<unknown>(WELCOME_PREFERENCES_KEY);
  const backup = nativePair ? parseJson(nativePair.backup) : null;
  const stored = current ?? backup;
  return stored
    ? sanitizeWelcomePreferences(stored)
    : DEFAULT_WELCOME_PREFERENCES;
};

export const persistWelcomePreferences = async (
  preferences: WelcomePreferences,
): Promise<void> => {
  const sanitized = sanitizeWelcomePreferences(preferences);
  if (isNativeDesktop()) {
    await invokeNative<void>("save_welcome_preferences", {
      payload: JSON.stringify(sanitized),
    });
    return;
  }
  await saveBrowserValue(WELCOME_PREFERENCES_KEY, sanitized);
};
