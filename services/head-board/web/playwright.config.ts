import { defineConfig, devices } from "@playwright/test";
import { fileURLToPath } from "node:url";
const root = fileURLToPath(new URL("../", import.meta.url));
if (!process.env.HEAD_E2E_DATA_DIR)
  throw new Error("HEAD_E2E_DATA_DIR must be an isolated QA directory");
export default defineConfig({
  testDir: "./e2e",
  fullyParallel: false,
  workers: 1,
  retries: 0,
  reporter: [["list"], ["json", { outputFile: "test-results/results.json" }]],
  use: {
    baseURL: "http://127.0.0.1:28445",
    trace: "retain-on-failure",
    screenshot: "only-on-failure",
  },
  projects: [{ name: "chromium", use: { ...devices["Desktop Chrome"] } }],
  webServer: {
    command: `"${root}.venv/bin/python" -m uvicorn headboard.api:configured_app --factory --host 127.0.0.1 --port 28445 --no-access-log`,
    cwd: root,
    url: "http://127.0.0.1:28445/api/health",
    reuseExistingServer: false,
    env: {
      HEAD_DATA_DIR: process.env.HEAD_E2E_DATA_DIR,
      HEAD_PUBLIC_URL: "http://127.0.0.1:28445",
    },
    timeout: 30000,
  },
});
