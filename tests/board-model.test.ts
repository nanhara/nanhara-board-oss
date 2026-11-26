import { describe, expect, it } from "vitest";
import {
  DEFAULT_WELCOME_PREFERENCES,
  createBoardDocument,
  isBoardDocument,
  sanitizeWelcomePreferences,
} from "../src/lib/board-model";

describe("board document recovery validation", () => {
  it("accepts a newly created document", () => {
    expect(isBoardDocument(createBoardDocument())).toBe(true);
  });

  it("rejects documents whose active page is missing", () => {
    const document = createBoardDocument();
    expect(isBoardDocument({ ...document, activePageId: "missing" })).toBe(false);
  });

  it("rejects a corrupted serialized scene so a backup can be used", () => {
    const document = createBoardDocument();
    document.pages[0] = { ...document.pages[0], scene: "{broken" };
    expect(isBoardDocument(document)).toBe(false);
  });
});

describe("welcome preference sanitization", () => {
  it("keeps only local image assets and safe timing values", () => {
    const value = sanitizeWelcomePreferences({
      enabled: "yes",
      idleMinutes: -1,
      slideSeconds: 1,
      headline: "a".repeat(100),
      subline: "b".repeat(200),
      assets: [
        { id: "safe", name: "welcome.png", dataUrl: "data:image/png;base64,AA==" },
        { id: "remote", name: "remote", dataUrl: "https://example.com/a.png" },
      ],
    });

    expect(value.enabled).toBe(DEFAULT_WELCOME_PREFERENCES.enabled);
    expect(value.idleMinutes).toBe(DEFAULT_WELCOME_PREFERENCES.idleMinutes);
    expect(value.slideSeconds).toBe(DEFAULT_WELCOME_PREFERENCES.slideSeconds);
    expect(value.headline).toHaveLength(40);
    expect(value.subline).toHaveLength(80);
    expect(value.assets.map((asset) => asset.id)).toEqual(["safe"]);
    expect(value.contentMode).toBe("images");
    expect(value.boardPageId).toBeNull();
  });

  it("preserves a referenced board page without embedding a second scene", () => {
    const value = sanitizeWelcomePreferences({
      ...DEFAULT_WELCOME_PREFERENCES,
      contentMode: "board-page",
      boardPageId: "page-1",
    });

    expect(value.contentMode).toBe("board-page");
    expect(value.boardPageId).toBe("page-1");
  });
});
