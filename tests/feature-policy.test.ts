import { describe, expect, it } from "vitest";
import { getFeatureAccess, PRODUCT_APP_ID } from "../src/lib/feature-policy";

describe("feature policy", () => {
  it("uses the stable product identity", () => {
    expect(PRODUCT_APP_ID).toBe("nanhara-board");
  });

  it("keeps every MVP capability free without login", () => {
    for (const feature of [
      "board.core",
      "board.presentationImport",
      "board.welcomeMode",
      "board.localRecovery",
    ] as const) {
      expect(getFeatureAccess(feature)).toEqual({ available: true, reason: "free" });
    }
  });

  it("reserves licensing only for future premium capabilities", () => {
    expect(getFeatureAccess("premium.cloudSync")).toEqual({
      available: false,
      reason: "future-premium",
    });
    expect(
      getFeatureAccess("premium.cloudSync", new Set(["premium.cloudSync"])),
    ).toEqual({ available: true, reason: "entitled" });
  });
});
