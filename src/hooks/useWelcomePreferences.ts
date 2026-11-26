import { useCallback, useEffect, useState } from "react";
import {
  DEFAULT_WELCOME_PREFERENCES,
  type WelcomePreferences,
} from "../lib/board-model";
import {
  loadWelcomePreferences,
  persistWelcomePreferences,
} from "../lib/persistence";

export const useWelcomePreferences = () => {
  const [preferences, setPreferences] = useState<WelcomePreferences | null>(null);
  const [persistenceError, setPersistenceError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    void loadWelcomePreferences()
      .then((value) => {
        if (!cancelled) setPreferences(value);
      })
      .catch(() => {
        if (cancelled) return;
        setPreferences(DEFAULT_WELCOME_PREFERENCES);
        setPersistenceError("欢迎屏本地设置暂时无法读取");
      });
    return () => {
      cancelled = true;
    };
  }, []);

  const updatePreferences = useCallback(async (value: WelcomePreferences) => {
    try {
      await persistWelcomePreferences(value);
      setPreferences(value);
      setPersistenceError(null);
    } catch (error) {
      setPersistenceError("欢迎屏设置未能写入本机，请检查磁盘空间");
      throw error;
    }
  }, []);

  return { preferences, updatePreferences, persistenceError };
};
