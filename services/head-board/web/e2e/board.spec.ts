import { test, expect } from "@playwright/test";
import { execFileSync } from "node:child_process";
import { readFileSync, mkdirSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { randomUUID } from "node:crypto";
const root = fileURLToPath(new URL("../../", import.meta.url));
const data = process.env.HEAD_E2E_DATA_DIR!;
const evidence =
  process.env.HEAD_EVIDENCE_DIR || join(root, "web/test-results");
function ownerCode() {
  const path = join(data, `code-${randomUUID()}.txt`);
  execFileSync(
    join(root, ".venv/bin/python"),
    ["-m", "headboard.cli", "--data", data, "owner-login", "--output", path],
    { cwd: root },
  );
  return readFileSync(path, "utf8").trim();
}

test("real UI: login, scopes, policy save, device ACK, revoked head and responsive layout", async ({
  page,
  browser,
}) => {
  test.setTimeout(90000);
  mkdirSync(evidence, { recursive: true });
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  await expect(
    page.getByRole("heading", { name: "Welcome back" }),
  ).toBeVisible();
  await page.getByLabel("Access code", { exact: true }).fill(ownerCode());
  await page.getByRole("button", { name: "Open control board" }).click();
  await expect(
    page.getByRole("heading", { name: "Managed users", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Add user", exact: true }).click();
  let modal = page.getByRole("dialog");
  await modal.getByLabel("Display name").fill("QA Synthetic Driver");
  await modal.getByLabel("Telegram user ID").fill("90001");
  await modal.getByRole("button", { name: "Create user" }).click();
  await expect(
    page.getByRole("heading", { name: "QA Synthetic Driver" }),
  ).toBeVisible();
  await page.getByLabel("Chat type").selectOption("channel");
  await page.getByLabel("Chat Telegram ID").fill("12345");
  await page.getByLabel("Chat display name").fill("QA Operations");
  await page.getByRole("button", { name: "Add to list", exact: true }).click();
  await page.getByRole("button", { name: "Save changes", exact: true }).click();
  await page
    .getByRole("dialog")
    .getByRole("button", { name: "Confirm and save" })
    .click();
  await expect(page.getByRole("status")).toContainText("Revision 1 saved");
  await page
    .getByRole("button", { name: "Head accounts", exact: true })
    .click();
  await page.getByRole("button", { name: "Add head", exact: true }).click();
  modal = page.getByRole("dialog");
  await modal.getByLabel("Display name").fill("QA Head");
  await modal.getByLabel("Telegram user ID").fill("90002");
  await modal.getByRole("button", { name: "Create head" }).click();
  const headRow = page.getByRole("row").filter({ hasText: "QA Head" });
  await headRow.getByRole("button", { name: "Sign-in code" }).click();
  const headCode = await page
    .getByLabel("Private code", { exact: true })
    .inputValue();
  await page.getByRole("button", { name: "Close dialog" }).click();
  await page
    .getByRole("button", { name: /Managed users/ })
    .first()
    .click();
  await page
    .getByRole("button", { name: "QA Synthetic Driver", exact: true })
    .click();
  await page.getByRole("checkbox", { name: /QA Head/ }).check();
  await page.getByRole("button", { name: "Save assignments" }).click();
  await expect(page.getByRole("status")).toContainText(
    "Head assignments updated",
  );
  await page.getByRole("button", { name: "Pair device", exact: true }).click();
  const invitation = await page
    .getByLabel("Private code", { exact: true })
    .inputValue();
  const metadata = JSON.parse(
    Buffer.from(invitation.slice(5), "base64url").toString("utf8"),
  );
  await page.getByRole("button", { name: "Close dialog" }).click();
  const enrolled = await page.request.post("/api/client/enroll", {
    data: {
      code: metadata.code,
      telegram_user_id: "90001",
      initial_peers: [],
      device_name: "Synthetic protocol fixture",
      client_version: "QA-only-not-native",
    },
  });
  expect(enrolled.status()).toBe(200);
  const device = await enrolled.json();
  const { createHash, createPublicKey, verify } = await import("node:crypto");
  const bytes = Buffer.from(device.policy.signed, "base64url");
  const publicKey = createPublicKey({
    key: { kty: "OKP", crv: "Ed25519", x: metadata.public_key },
    format: "jwk",
  });
  expect(
    verify(
      null,
      Buffer.concat([Buffer.from("ALLOWGRAM_HEAD_POLICY_V1\n"), bytes]),
      publicKey,
      Buffer.from(device.policy.signature, "base64url"),
    ),
  ).toBe(true);
  expect(JSON.parse(bytes.toString()).peers).toEqual([
    { kind: "channel", id: "12345" },
  ]);
  const ack = await page.request.post("/api/client/ack", {
    headers: { Authorization: `Bearer ${device.device_token}` },
    data: {
      revision: 1,
      policy_sha256: createHash("sha256").update(bytes).digest("hex"),
    },
  });
  expect(ack.status()).toBe(200);
  await page.getByRole("button", { name: "Reload saved list" }).click();
  await expect(
    page.getByText("Applied", { exact: true }).first(),
  ).toBeVisible();
  await expect(
    page.getByText("Synthetic protocol fixture", { exact: true }),
  ).toBeVisible();
  await page.screenshot({
    path: join(evidence, "board-desktop-synthetic.png"),
    fullPage: true,
  });
  await page.setViewportSize({ width: 390, height: 844 });
  await expect(
    page.getByRole("button", { name: "Save changes", exact: true }),
  ).toBeVisible();
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= window.innerWidth,
    ),
  ).toBe(true);
  await page.screenshot({
    path: join(evidence, "board-mobile-synthetic.png"),
    fullPage: true,
  });
  await page.setViewportSize({ width: 1280, height: 900 });
  const context = await browser.newContext();
  const head = await context.newPage();
  await head.goto("http://127.0.0.1:28445/");
  await head.getByLabel("Access code", { exact: true }).fill(headCode);
  await head.getByRole("button", { name: "Open control board" }).click();
  await expect(
    head.getByRole("button", { name: "QA Synthetic Driver", exact: true }),
  ).toBeVisible();
  await expect(
    head.getByRole("button", { name: "Head accounts", exact: true }),
  ).toHaveCount(0);
  await expect(
    head.getByRole("button", { name: "Add user", exact: true }),
  ).toHaveCount(0);
  await page
    .getByRole("button", { name: "Head accounts", exact: true })
    .click();
  page.once("dialog", (dialog) => dialog.accept());
  await page
    .getByRole("row")
    .filter({ hasText: "QA Head" })
    .getByRole("button", { name: "Disable", exact: true })
    .click();
  await head.reload();
  await expect(
    head.getByRole("heading", { name: "Welcome back" }),
  ).toBeVisible();
  await context.close();
  await page.getByRole("button", { name: "Activity", exact: true }).click();
  await expect(
    page.getByText("policy · acknowledged", { exact: true }),
  ).toBeVisible();
  await page.screenshot({
    path: join(evidence, "board-audit-synthetic.png"),
    fullPage: true,
  });
  await page.getByRole("button", { name: "Sign out", exact: true }).click();
  await expect(
    page.getByRole("heading", { name: "Welcome back" }),
  ).toBeVisible();
  expect(errors).toEqual([]);
});

test("failed sign-in remains anonymous and source/static traversal exposes no secrets", async ({
  page,
}) => {
  await page.goto("/");
  await page
    .getByLabel("Access code", { exact: true })
    .fill("invalid-code-no-capability-00000000");
  await page.getByRole("button", { name: "Open control board" }).click();
  await expect(page.getByRole("alert")).toContainText("Invalid or expired");
  expect((await page.request.get("/api/users")).status()).toBe(401);
  for (const path of ["/headboard/store.py", "/.env", "/../headboard/store.py"])
    expect((await page.request.get(path)).status()).toBe(404);
});
