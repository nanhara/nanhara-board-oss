import { useEffect } from "react";

const ACTIVITY_EVENTS: Array<keyof WindowEventMap> = [
  "pointerdown",
  "pointermove",
  "keydown",
  "wheel",
];

export const useIdleWelcome = ({
  enabled,
  idleMinutes,
  paused,
  onIdle,
}: {
  enabled: boolean;
  idleMinutes: number;
  paused: boolean;
  onIdle: () => void;
}) => {
  useEffect(() => {
    if (!enabled || paused || idleMinutes <= 0) return;

    let timer = window.setTimeout(onIdle, idleMinutes * 60_000);
    const reset = () => {
      window.clearTimeout(timer);
      timer = window.setTimeout(onIdle, idleMinutes * 60_000);
    };

    ACTIVITY_EVENTS.forEach((eventName) =>
      window.addEventListener(eventName, reset, { passive: true }),
    );
    document.addEventListener("visibilitychange", reset);

    return () => {
      window.clearTimeout(timer);
      ACTIVITY_EVENTS.forEach((eventName) =>
        window.removeEventListener(eventName, reset),
      );
      document.removeEventListener("visibilitychange", reset);
    };
  }, [enabled, idleMinutes, onIdle, paused]);
};
