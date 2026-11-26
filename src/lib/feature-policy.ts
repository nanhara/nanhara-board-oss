export const PRODUCT_APP_ID = "nanhara-board";

export type FeatureId =
  | "board.core"
  | "board.presentationImport"
  | "board.welcomeMode"
  | "board.localRecovery"
  | "premium.cloudSync"
  | "premium.aiAssistant";

const FREE_FEATURES = new Set<FeatureId>([
  "board.core",
  "board.presentationImport",
  "board.welcomeMode",
  "board.localRecovery",
]);

export type FeatureAccess = {
  available: boolean;
  reason: "free" | "entitled" | "future-premium";
};

export const getFeatureAccess = (
  feature: FeatureId,
  entitlements: ReadonlySet<FeatureId> = new Set(),
): FeatureAccess => {
  if (FREE_FEATURES.has(feature)) {
    return { available: true, reason: "free" };
  }

  if (entitlements.has(feature)) {
    return { available: true, reason: "entitled" };
  }

  return { available: false, reason: "future-premium" };
};
