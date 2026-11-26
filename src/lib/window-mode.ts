let wakeLock: WakeLockSentinel | null = null;

export const enterWelcomeWindowMode = async () => {
  try {
    if (window.__TAURI_INTERNALS__) {
      const { getCurrentWindow } = await import("@tauri-apps/api/window");
      await getCurrentWindow().setFullscreen(true);
    } else if (document.fullscreenElement === null) {
      await document.documentElement.requestFullscreen?.();
    }
  } catch {
    // Automatic browser fullscreen may be rejected without a user gesture.
  }

  try {
    wakeLock = await navigator.wakeLock?.request("screen");
  } catch {
    wakeLock = null;
  }
};

export const leaveWelcomeWindowMode = async () => {
  try {
    await wakeLock?.release();
  } catch {
    // Releasing an already-released lock is harmless.
  }
  wakeLock = null;

  try {
    if (window.__TAURI_INTERNALS__) {
      const { getCurrentWindow } = await import("@tauri-apps/api/window");
      await getCurrentWindow().setFullscreen(false);
    } else if (document.fullscreenElement) {
      await document.exitFullscreen?.();
    }
  } catch {
    // The app remains usable even if the window manager refuses the request.
  }
};
